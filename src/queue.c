#include "q.h"
#include "dlc_string.h"

#include <sys/types.h>
#include <pwd.h>
#include <time.h>
#include <sys/wait.h>
#include <stdlib.h>

#include "list.h"

/* the master server collects, organizes, and eventually schedules
   jobs sent by enqueue.  the goal is to be event driven

   event           action
   -----           -------------------------------
   job is enqueud  try to find a host to run it on
                     or record it on the queue
   job ends        try to find a job on the queueu
                     to replace it with on the host   
*/

extern const sendhdr_t hdr_zero;

static inline int cmp_timespec(struct timespec first,struct timespec second)
  {
  if (second.tv_sec == first.tv_sec)
    return signed_step( second.tv_nsec - first.tv_nsec );
  return signed_step(second.tv_sec - first.tv_sec);
  }

static inline int later(struct timespec first,struct timespec second)
  {
  if (second.tv_sec == first.tv_sec) return second.tv_nsec > first.tv_nsec;
  return second.tv_sec > first.tv_sec;
  }
static inline int earlier(struct timespec first,struct timespec second)
  {
  if (second.tv_sec == first.tv_sec) return second.tv_nsec < first.tv_nsec;
  return second.tv_sec < first.tv_sec;
  }

void print_user_list(joblink_t *it)
  {
  uidlink_t *ul = uidlist_head();
  int uc = 0;

  for (;ul && uc<10 ; ul=ul->un,uc++) 
    {
    printf("user %d %d %p %p (%p)\n",uc,ul->uid,ul->head,ul->tail,it);
    joblink_t *jl = ul->head;
    int c=0;
    for (; jl && c<10 ; jl=jl->un,c++)
      {
      printf("    c %d h %p p %p it %p n %p t %p: %s\n",
             c,
             jl->u->head,jl->up,jl,jl->un,jl->u->tail,
             jl->dir);
      }
    }
  }

// open a files in a job dir and also return its size
int openjob(const char *dir,const char *file,int flags,off_t *filesize)
  {
  int pathlen = strlen(dir);
  int filelen = strlen(file);
  char pathname[pathlen+1+filelen+1];
  strcpy(pathname,dir);
  pathname[pathlen]='/';
  strcpy(pathname+pathlen+1,file);

  
  int fd = open(pathname,flags,0666);
  if (fd<0)
    {
    if (filesize) *filesize = 0;
    return fd;
    }

#ifdef regfd    
  regfd("open",fd);
#endif  
  if (filesize)
    {
    struct stat statbuf;
    statbuf.st_size = 0;

    int tries=0;
    while (fstat(fd,&statbuf)) 
      {
      if (tries>30)
        {
        close(fd);
        fprintf(stderr,"openjob(%d) %s %s %x %d\n",
                tries,dir,file,flags,fd);
        perror("fstat failed");
        return -1;
        }
      usleep(10000);
      tries++;
      }
    
    *filesize = statbuf.st_size;
    }
  
  return fd;
  }

/* read the parm file - a number of parms deal with
   matters important to job requirements */
int read_parse_parms(const char *dir,char ***parmsp)
  {
  off_t parmsize;
  
  int pfd = openjob(dir,"parm",O_RDONLY,&parmsize);
  if (pfd<0) return 0;
  
  char *pbuf = malloc(parmsize);
  read(pfd,pbuf,parmsize);
  close(pfd);
  
  int nparms=0;
  for (int i=0;i<parmsize;i++) if (pbuf[i]==0) nparms++;

  char **parms = *parmsp = malloc(sizeof(char *)*2*(nparms+1));
  parms[0] = pbuf;
  
  for (int i=0,n=1;i<parmsize;i++)
    if (pbuf[i] == ((n&1) ? '=' : 0))
      {
      pbuf[i] = 0;
      parms[n++] = pbuf+i+1;
      }
  return nparms;
  }

// search parms for a parm - return pointer to string value
const char *find_parm(const char *name,
                      int nparms, char **parml,
                      const char *def)
  {
  for (int i=0;i<nparms;i++)
    if (! strcmp(name,parml[2*i]))
      return parml[2*i+1];
  
  return def;
  }

// search parms for a parm - return integer value
typedef struct multsuf_s
  {
  char s;
  int m;
  } multsuf_t;

int find_int_parm(const char *name,
                  int nparms, char **parml,
                  int def,multsuf_t *suftab)
  {
  int debug=0;
  
  for (int i=0;i<nparms;i++)
    if (! strcmp(name,parml[2*i]))
      {
      char *suf=0;
      int val = strtol(parml[2*i+1],&suf,0);
      if (debug) fprintf(stderr,"fip %s: val %d\n",name,val);
      
      if (suf)
        for (multsuf_t *s=suftab;s;s++) 
          {
          if (s->s==0) break;
          if (*suf==s->s)
            {
            val*=s->m;
            if (debug) fprintf(stderr,"  suf %c %d: val %d\n",suf,s->s,val);
            suf++;
            }
          }
      return val;
      }
  
  return def;
  }

// return contents of status query via server_status
statusinfomsg_t *get_myhost_status(int mypid,statusinfomsg_t **psi)
  {

  /* if caller doesn have a place to store it
     provide a static location */
  static statusinfomsg_t *sis;  
  if (!psi) psi = &sis;

  queuestatus_t *ques = readqueuestatus(mypid);

  if (*psi==NULL || ques->n > (*psi)->srs)
    {
    int nrs = ques->n+16;
    size_t newsize = ( sizeof(statusinfomsg_t)+
                       nrs*sizeof(running_stats_t));
    
    *psi = realloc(*psi, newsize);
    memset(*psi,0,newsize);
    (*psi)->srs = nrs;
    }

  statusinfomsg_t *si = *psi;

  si->nrs = ques->n;
  memcpy(si->runs,ques->s,sizeof(running_stats_t)*si->nrs);
  
  si->info = readcpuinfo();
  si->stat = readcpustatus();
  si->user = readuserinfo();
  
  return si;
  }

/* send getstatus query to a host and recieve a reply,
   unless the query is for the local host, in which case
   just run get_myhost_status */
statusinfomsg_t *get_host_status(const char *host,int client,
                                 statusinfomsg_t **psi)
  {
  if (!client && !strcmp(host,hostname()))
    {
    int srvpid = 0;
    return get_myhost_status(srvpid,psi);
    }

  /* if caller doesn have a place to store it
     provide a static location */
  static statusinfomsg_t *sis=0;
  if (!psi) psi = &sis;  
  
  sendhdr_t hdr;
  if (get_magic_for_host(host,&hdr.magic)) return NULL;
  
  int port = getserviceport();
  int sockfd = open_client_socket(host,port,host);
  
  if (sockfd<0) return NULL;

  hdr.uid   = getuid();
  hdr.gid   = getgid();
  hdr.kind  = DK_getstatus;
  hdr.size  = 0;

  send(sockfd,&hdr,sizeof(hdr),0);
  recvn(sockfd,&hdr,sizeof(hdr),0);

  if (*psi==NULL || hdr.size> (*psi)->srs)
    {
    *psi = realloc(*psi,hdr.size);
    (*psi)->srs = hdr.size;
    }

  statusinfomsg_t *si = *psi;
  int srs = si->srs;
  recvn(sockfd,si,hdr.size,0);
  
  close(sockfd);  

  si->srs = srs;
  
  return si;
  }

statusinfomsg_t *get_hl_status(hostlink_t *hl,int client,time_t recent)
  {
  time_t now = time(NULL);
  if (hl->si && recent && now < hl->sift + recent) return hl->si;
  
  return get_host_status(hl->host,client,&(hl->si));
  }

/* a job has been scheduled, send it to a host to be run */
void send_job_host(const char *host,joblink_t *jl)
  {
  sendhdr_t hdr = hdr_zero;
  
  hdr.kind = DK_runjob;
  hdr.size = strlen(jl->dir)+1;
  hdr.uid = jl->u->uid;
  hdr.gid = jl->gid;
  set_tag_ui32x2(hdr.value,jl);

  if (0)
    printf("send_job_host %d\n",hdr.uid);
  
  simple_request(host,"sendjob",&hdr,jl->dir);
  }

/* send a kill request for a job to the host it is running on */
int send_kill(const char *host,joblink_t *jl)
  {
  sendhdr_t hdr = hdr_zero;
  
  hdr.kind = DK_killjob;
  hdr.uid = jl->u->uid;
  hdr.gid = jl->gid;
  set_tag_ui32x2(hdr.value,jl);
  if (0)
    printf("send_kill %d %s %p %s\n",
           hdr.uid,host,jl,jl->dir);
  simple_request(host,"killjob",&hdr,NULL);
  return hdr.value[3];
  }

/* override host metrics from conf */
typedef struct
  {
  int memory,buf,threads,busy_start,busy_end;
  } limits_t;

void host_limits(limits_t *l,conf_t *conf,const char *host,statusinfomsg_t *si)
  {
  confl_t *limits = conf_find(conf,"limits",host,NULL);
  l->memory = si->info.memory; // MB
  l->buf    = si->stat.membufrc; // MB
  l->threads = si->info.threads;
  l->busy_start = -1;  // sec in day
  l->busy_end   = -1;
  for (; limits ; limits=limits->next)
    {
    if (!strncmp(limits->name,"mem=",4))
      {
      char *suf=0;
      int mem = strtol(limits->name+4,&suf,0);
      if (suf && *suf=='G') mem*=1024;
      if (mem<l->memory) l->memory = mem;
      }
    if (!strncmp(limits->name,"buf=",4))
      {
      char *suf=0;
      int buf = strtol(limits->name+4,&suf,0);
      if (suf && *suf=='G') l->buf = buf*1024;
      }
    if (!strncmp(limits->name,"threads=",8))
      {
      char *suf=0;
      int thr = strtol(limits->name+8,&suf,0);
      if (thr<l->threads) l->threads = thr;
      }
    if (!strncmp(limits->name,"cores=",6))
      {
      char *suf=0;
      int thr = strtol(limits->name+6,&suf,0)*2;
      if (thr<l->threads) l->threads = thr;
      }
    if (!strncmp(limits->name,"busy=",5))
      {
      char *suf=0;
      int val = strtol(limits->name+5,&suf,10)*60*60;
      if (suf>limits->name+5)
        {
        l->busy_start = val;
        if (suf && *suf==':')
          {
          int m = strtol(suf+1,&suf,10);
          l->busy_start += m*60;
          }
        if (suf && *suf)
          {
          l->busy_end = strtol(suf+1,&suf,10)*60*60;
          if (*suf==':')
            {
            int m = strtol(suf+1,&suf,10);
            l->busy_end += m*60;
            }
          }
        }
      }
    }
  }

/* decide if a job should be scheduled on a host */
int jl_fits_hl(conf_t *conf,
               hostlink_t *hl,
               statusinfomsg_t *si,
               joblink_t *jl)
  {
  int debug=0;
  
  // if cant get info, dont schedule  
  if (! jl) return 0;// no valid job

  limits_t lim;
  host_limits(&lim,conf,hl->host,si);
  
  // dont schedule if host is busy
  if (lim.busy_end>lim.busy_start)
    {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    int nows  = tm->tm_sec + tm->tm_min*60 + tm->tm_hour*60*60;
    
    if (debug) fprintf(stderr,"   sjhl-busy(%s): %d %d %d\n",
                     hl->host,nows,lim.busy_end,lim.busy_start);

    if (nows>=lim.busy_start && nows<=lim.busy_end) return 0;
    }
  
  // dont schedule if mem request is too big
  if (debug) fprintf(stderr,"   sjhl-mem(%s): %d %d %d\n",
                     hl->host,jl->mem,lim.memory,hl->used_memory);

  // note: used+avail=total & free+buf=avail
  
  // mem req exceeds mem available for another job
  if (jl->mem > lim.memory - hl->used_memory) return 0;
  
  // mem req exceeds mem available for any new process
  if (jl->mem > si->stat.memavail - lim.buf) return 0;
      
  if (debug) fprintf(stderr,"   sjhl-thr(%s): %d %d %d\n",
                     hl->host,jl->threads,lim.threads,hl->used_threads);

  // threads request 
  int threads = jl->threads;
  if (threads==0) threads = lim.threads;

  // dont schedule if threads request is too big
  int available_threads_h = si->info.threads -
                            hl->used_threads -
                            si->runs[0].running;
  int available_threads_l = lim.threads -
                            hl->used_threads;
  
  if (threads > available_threads_h ||
      threads > available_threads_l ) return 0;

  if (0)
    fprintf(stderr,"   sjhl-ok %d\n",threads);
  return threads;
  }

/* see if a job could be schedule on a host */
typedef struct
  {
  joblink_t *jl;
  int        flaws; // boolean flags decoded by enqueue_client
  } jl_can_host_info_t;
  
int jl_can_host(conf_t *conf,const char *host,void *info)
  {
  int debug=0;
  
  hostlink_t *hl = get_host_state(host);
  jl_can_host_info_t *ii = info;
  joblink_t *jl = ii->jl;

  ii->flaws += 0x100;
  
  limits_t lim;
  statusinfomsg_t *si = get_hl_status(hl,0,2);
  if (!si) return 0;                 // get_host_status failed...
  if (si->info.cores==0) return 0;   // get_host_status failed...
  
  host_limits(&lim,conf,host,si);

  //hostlink_t *hl = get_host_state(host);
  
  if (debug) fprintf(stderr,"jch: %p %s %s %d\n",
                     jl,jl->dir,host,si->info.cores);

  if (debug) fprintf(stderr,"   jch-mem(%s): %d %d",
                     host,jl->mem,lim.memory);

  // cant schedule if memory request is too big
  if (jl->mem > lim.memory)
    {
    ii->flaws |= 2;
    if (debug) fprintf(stderr,"   jch-big\n");
    return 0;
    }

  if (jl->mem > si->stat.memavail - lim.buf)
    {
    ii->flaws |= 4;
    if (debug) fprintf(stderr,"   jch-sqz\n");
    return 0;
    }
  
  if (debug) fprintf(stderr,"   jch-thr(%s): %d %d",
                     host,jl->threads,lim.threads);

  // cant schedule if threads request is too big
  if (jl->threads > lim.threads )
    {
    ii->flaws |= 8;
    if (debug) fprintf(stderr,"   jch-thr\n");
    return 0;
    }
  
  if (debug) fprintf(stderr,"   jch-ok\n");
  
  return 1;
  }


typedef struct
  {
  joblink_t *jl;
  hostlink_t *hl;
  int threads;
  int memavail;
  int thravail;
  } hostpick_t;

/* find a job for an open spot on a host */
int pick_job_hl(conf_t *conf,
                hostlink_t *hl,
                statusinfomsg_t *si,
                hostpick_t *hp)
  {
  int debug = 0;
  
  joblink_t *jl = hp->jl;
  
  int threads = jl_fits_hl(conf,hl,si,jl);
  if (threads==0) return 0;
      
  // ok to schedule -- is this better than hp?
  int nrs = si->nrs;
  int ath = si->info.threads - hl->used_threads;
  if (hp->hl)
    {
    if (hp->memavail > si->stat.memavail)
      {
      if (debug) fprintf(stderr,"  host %s has more memavail than %s\n",
                         hp->hl->host,hl->host);
      return 0;
      }
    
    if (hp->thravail > ath)
      {
      if (debug) fprintf(stderr,"  host %s has more thravail than %s\n",
                         hp->hl->host,hl->host);
      return 0;
      }
    if (debug) fprintf(stderr,"  host %s is not better than %s\n",
                       hp->hl->host,hl->host);
    }
  else
    {
    if (debug) fprintf(stderr,"  first host %s\n",hl->host);
    }
  
  // yes it is better than hl
  hp->hl = hl;
  hp->threads = threads;
  hp->memavail = si->stat.memavail;
  hp->thravail = ath;

  // if hl is completely idle -- just schedule it
  int goodenough = hl->jobs_running==0 && si->user.users==0;
  if (debug && goodenough)
    {
    fprintf(stderr,"  goodenough %s\n",hl->host);
    }
  
  return goodenough;
  }

/* do the accounting for assigning a job to a host and
   either send it there or launch it on this host */
int schedule_job_hl(conf_t *conf,
                    hostlink_t *hl,
                    joblink_t *jl,
                    int threads)
  {
  // do accounting
  if (! hl) return 0; // hl was not picked

  // fprintf(stderr,"scheduling %p %s on %p %s\n",jl,jl->dir,hl,hl->host);
  
  jl->threads = threads;

  claim_tokens(jl);
  
  hl->used_memory += jl->mem;
  hl->used_threads += jl->threads;
  hl->jobs_running ++;

  jl->u->threads += jl->threads;
  clock_gettime(CLOCK_REALTIME, &jl->u->l_start);
  
  // put job in host list
  add_link_to_head(hl,jl,h);
  
  // send the job
  if (strcmp(hl->host,hostname()))
    { // remote host
    if (0) fprintf(stderr,"   sjhl-remote: %p %d\n",jl,jl->u->uid);
    send_job_host(hl->host,jl);
    }
  else
    { // local host -- fork a control thread
    if (0) fprintf(stderr,"   sjhl-local: \n");
    jl->tag = (uint64_t)jl;
    pthread_detached(&launch_control,jl);
    }

  return 1;  
  }

/* get status for and try to job for a host */
int pick_job_host(conf_t *conf,const char *host,void *info)
  {
  hostpick_t *hp = info;
  
  hostlink_t *hl = get_host_state(host);
  statusinfomsg_t *si = get_hl_status(hl,0,2);
  
  if (!si) return 0;   // get_host_status failed...
  
  joblink_t *jl = hp->jl;
  if (0) fprintf(stderr,"sjh: %s %s %d\n",jl->dir,host,si->info.cores);

  if (si->info.cores==0) return 0;   // get_host_status failed...
  
  return pick_job_hl(conf,hl,si,hp);
  }

/* have a job -- find a host for it now */
int schedule_job(conf_t *conf,joblink_t *jl)
  {
  hostpick_t hp;

  if (!check_tokens(jl)) return 0;
  
  hp.jl=jl;
  hp.hl=0;
  
  int picked = for_each_host(conf,
                             find_parm("group",jl->nparms,jl->parms,"all"),
                             pick_job_host,
                             &hp);

  return schedule_job_hl(conf,hp.hl,hp.jl,hp.threads);
  }

/* stat the job dir to get submit time */
struct timespec get_job_time(joblink_t *jl)
  {
  struct stat jstat;
  stat(jl->dir,&jstat);
  return jstat.st_ctim;
  }

/* insert job into q maintaining priority and age order
   priority 0 schedules before priority 7 */
static inline int sort_order(joblink_t *jl,joblink_t *p)
  {
  if (jl->pri>p->pri) return 1;
  if (jl->pri==p->pri && earlier(jl->ct,p->ct)) return 1;
  return 0;
  }

void insert_into_list_sorted_fromh(uidlink_t *ul,joblink_t *jl)
  {
  // search for spot in user list between older job and newer job
  joblink_t *p;
  for (p=ul->head; p && sort_order(jl,p) ; p=p->un);
  
  // put job in order from newest to oldest
  if (p) add_link_before(p,jl,u);
  else   add_link_to_tail(ul,jl,u);
  
  jl->u = ul;
  }

void insert_into_list_sorted_fromt(uidlink_t *ul,joblink_t *jl)
  {
  // search for spot in user list between older job and newer job
  joblink_t *p;
  for (p=ul->tail; p && !sort_order(jl,p) ; p=p->up);
  
  // put job in order from newest to oldest
  if (p) add_link_after(p,jl,u);
  else   add_link_to_head(ul,jl,u);
  
  jl->u = ul;
  }

void insert_into_list_sorted(uidlink_t *ul,joblink_t *jl)
  {
  if (ul->head==NULL)
    { // first one
    add_link_to_head(ul,jl,u);
    return;
    }

  if (jl->pri == ul->tail->pri)
    { // closer to tail
    insert_into_list_sorted_fromt(ul,jl);
    return;
    }
  
  if (jl->pri == ul->head->pri)
    {
    // closer to head
    insert_into_list_sorted_fromh(ul,jl);
    return;
    }
  
  // closer to tail?
  insert_into_list_sorted_fromt(ul,jl);
  }


/* recieve a jobdir from enqueue */
joblink_t *recieve_jobinfo(sendhdr_t *hdr,
                           const char *dir,
                           int sock)
  {
  size_t msgsize = hdr->size;
  
  joblink_t *jl = make_jl();
  
  jl->gid = hdr->gid;
  jl->jg  = hdr->value[0];
  jl->pri = hdr->value[1];
  
  jl->dir = malloc(strlen(dir)+1+msgsize+1);
  char *dp = cpystring(jl->dir,dir);
  *dp++ = '/';
  
  recvn(sock,dp,msgsize,0);  // now have jobid
  
  return jl;
  }

// master side of enqueue.c: enqueue_client
multsuf_t Msuf_tab[3] = 
  {
  {'G', 1024},
  { 0, 0}
  };

void server_enqueue(server_thread_args_t *client)
  { // on a sync thread in the server space
  sendhdr_t hdr = client->hdr;
  int sock = client->sock;

  // Append job to database
  uidlink_t *ul = find_or_make_uid(hdr.uid);
  joblink_t *jl = recieve_jobinfo(&hdr,ul->dir,sock);

  // make sure cmd is present
  sendhdr_t *chdr=get_job_cmd(jl->dir,NULL);
  if (! chdr)
    { // not yet, have to reject back to client, it
    // can wait and send request back...
    hdr.kind = DK_reject;
    hdr.size = 0;
    send_response(sock,&hdr,NULL);
    return;
    }
  
  // message didnt beat nfs....
  free(chdr);
  
  // read and parse parms
  jl->nparms  = read_parse_parms(jl->dir,&(jl->parms));
  jl->mem     = find_int_parm("mem",jl->nparms,jl->parms,1,Msuf_tab); //mb
  jl->threads = find_int_parm("threads",jl->nparms,jl->parms,1,NULL); //single

  // reject if job is not schedulable
  jl_can_host_info_t info;
  info.flaws = 0;
  info.jl    = jl;

  int tok_ok=check_tokens_ever(jl);
  if (!tok_ok) info.flaws |= 1;
  
  int picked = for_each_host(client->conf,
                             find_parm("group",jl->nparms,jl->parms,"all"),
                             jl_can_host,
                             &info);

  if (!tok_ok || !picked)
    {  // send reject response to client
    hdr.kind = DK_reply;
    hdr.size = 0;
    hdr.value[0] = -1 ^ info.flaws;
  
    send_response(sock,&hdr,NULL);
    free_jl(jl);
    return;
    }

  // job create time of job directory
  jl->ct = get_job_time(jl);
  
  // enter it into the system
  insert_into_list_sorted(ul,jl);

  // printf("enqueueing uid %d %d %d\n",hdr.uid,ul->uid,jl->u->uid);
  
  // try to schedule it
  int sch_immed = schedule_job(client->conf,jl);

  // send response to client
  hdr.kind = DK_reply;
  hdr.size = 0;
  hdr.value[0] = sch_immed;
  
  send_response(sock,&hdr,NULL);
  }


int user_sorter(const void *a,const void *b)
  {
  const uidlink_t *ua = a;
  const uidlink_t *ub = b;

  // try to equalize running threads
  int urc = signed_step(ua->threads - ub->threads);
  if (urc) return urc;

  // try to give priority to most stale q
  int stc = cmp_timespec(ua->l_start,ub->l_start);
  if (stc) return stc;

  // no reason to pick
  return 0;
  }


void schedule_host(conf_t *conf,hostlink_t *hl)
  {
  uidlink_t *ul;

  // count number of users with jobs to run
  int uidn = 0;
  for (ul=uidlist_head() ; ul ; ul=ul->un)
    if (ul->head) uidn++;

  if (uidn==0) return;  // nobody with jobs to run

  // populate and sort uid list according to
  // number of running threads
  uidlink_t *uid[uidn];

  int n=0;
  for (ul=uidlist_head() ; ul ; ul=ul->un)
    if (ul->head) uid[n++] = ul;

  if (n>1) qsort(uid,n,sizeof(*uid),user_sorter);

  statusinfomsg_t *si = get_hl_status(hl,0,2);
  if (si->info.cores==0) return;

  // schedule and keep scheduling jobs on this host
  // until no more happens
  int onedone=1;
  while (onedone)
    {
    onedone = 0;
    int ui;
    for (ui=0;ui<n;ui++)
      {  
      joblink_t *jl;
      int threads;
      
      for (jl=uid[ui]->head ; jl ; jl = jl->un) // oldest job first
        if ( !jl->h && check_tokens(jl) &&
             (threads = jl_fits_hl( conf, hl, si, jl ) ))
          {
          schedule_job_hl(conf,hl,jl,threads);
          onedone ++;
          break;  // give next user a chance
          }
      }
    }
  }

// master receives notification job is done from
// server's launch_control
void server_jobdone(server_thread_args_t *client)
  {
  sendhdr_t hdr = client->hdr;
  int sock = client->sock;
  int status = hdr.value[3];
  joblink_t *jl = get_tag_ui32x2(hdr.value);

  if (0) fprintf(stderr,"sjd: %p\n",jl);
  
  // give back resources
  release_tokens(jl);
  
  hostlink_t *hl = jl->h;
  hl->used_memory  -= jl->mem;
  hl->used_threads -= jl->threads;
  hl->jobs_running -- ;

  uidlink_t *ul = jl->u;
  ul->threads -= jl->threads;

  remove_link(jl,h);  // unlink it from host list
  remove_link(jl,u);  // unlink it from uid list

  free_jl(jl);
  
  hdr.kind = DK_reply;
  hdr.size = 0;
  send_response(sock,&hdr,NULL);

  schedule_host(client->conf,hl);
  }

int append_host_state(conf_t *conf,const char *host,void *info)
  {
  dlc_string **r = (dlc_string **)info;
  hostlink_t *hl = get_host_state(host);
  statusinfomsg_t *si = get_hl_status(hl,0,2);
  
  dlc_string_caf(r,"%s %d %d %d %d\n",host,
                 hl->jobs_running,si->stat.la1,
                 si->info.threads - hl->used_threads,
                 si->info.memory - hl->used_memory);

  parsed_cmd_t pc;
  joblink_t *jl=hl->head;
  for (; jl ; jl=jl->hn)
    {
    sendhdr_t *hdr = get_job_cmd(jl->dir,&pc);
    if (! hdr) continue;
    dlc_string_caf(r,"  %s %d %d %s %s\n",
                   strrchr(jl->dir,'/')+1,
                   jl->threads,jl->mem,
                   jl->u->name,
                   pc.cmd);
    free(pc.env);
    free(hdr);
    }
  
  return 0;
  }

// queue status request... returns string to client in
// enqueue.c: listqueue_client
void server_lsq(server_thread_args_t *client)
  {
  dlc_string *response=0;
  sendhdr_t hdr = client->hdr;
  parsed_cmd_t pc;
  void dlc_tokens(dlc_string **stream);

  dlc_string_cat(&response,"tokens:\n");
  dlc_tokens(&response);

  dlc_string_cat(&response,"machines:\n");
  
  for_each_host(client->conf,
                "all",
                append_host_state,
                &response);

  dlc_string_cat(&response,"users:\n");
  uidlink_t *ul = uidlist_head();
  for ( ; ul ; ul = ul->un )
    { //foreach user
    joblink_t *jl=ul->head;
    
    dlc_string_caf(&response," %d %s %d\n",
                   ul->uid,ul->name,ul->threads);
    
    for (; jl ; jl=jl->un)
      { // foreach job
      // print job info
      sendhdr_t *hdr = get_job_cmd(jl->dir,&pc);
      if (!hdr) continue;
      
      dlc_string_caf(&response,"  %s %d %d %s %s\n",
                     strrchr(jl->dir,'/')+1,jl->threads,jl->mem,
                     jl->h ? jl->h->host : "NOTRUN",
                     pc.cmd);
      free(pc.env);
      free(hdr);
      }
    }
  
  hdr.size = dlc_string_len(response)+1;
  hdr.kind = DK_reply;
  send_response(client->sock,&hdr,response->t);
  dlc_string_free(&response);
  }

