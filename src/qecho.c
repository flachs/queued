#include "q.h"

char *simple_request(const char *hostname,const char *message,
                     sendhdr_t *hdr,void *data)
  {
  sendhdr_t orighdr = *hdr;
  if (orighdr.uid==0)   orighdr.uid = getuid();
  if (orighdr.gid==0)   orighdr.gid = getgid();

  char *retdata=0;
  retdata --;
  
  int port = getserviceport();

  int badret = 0;
  int sockfd = 0;
  
  while (1)
    {
    if (get_magic_for_host(hostname,& orighdr.magic))
      return retdata;
  
    sockfd = open_client_socket(hostname,port,message);
  
    if (sockfd<0) return retdata;
  
    send(sockfd,&orighdr,sizeof(orighdr),0);
    if (orighdr.size)
      send(sockfd,data,orighdr.size,0);
    
    recvn(sockfd,hdr,sizeof(*hdr),0);
    if (hdr->magic != DK_badmagic) break;
    if (badret++>2) return retdata;
    while (sleep(3));
    }
    
  retdata = 0;
  if (hdr->size)
    {
    retdata = malloc(hdr->size);
    recvn(sockfd,retdata,hdr->size,0);
    }
  
  close(sockfd);  
  return retdata;
  }

void server_echo(server_thread_args_t *client)
  {
  sendhdr_t hdr = client->hdr;
  int sock = client->sock;

  hdr.kind = DK_echorep;
  char buffer[hdr.size];
  
  recvn(sock,buffer,hdr.size,0);
  send_response(sock,&hdr,buffer);
  close(sock);
  }

int echo_client(conf_t *conf,int argn,char **argv,char **env)
  {
  char *hostname = *argv++; argn--;
  
  sendhdr_t hdr;
  if (get_magic_for_host(hostname,& hdr.magic)) return 1;
  
  hdr.uid   = getuid();
  hdr.gid   = getgid();
  hdr.kind  = DK_syncecho;
  
  int port = getserviceport();
  int sockfd = open_client_socket(hostname,port,hostname);
  if (sockfd<0) return 1;
  
  char *args = make_arg_string(argn,argv);
  hdr.size  = strlen(args)+1;

  send(sockfd,&hdr,sizeof(hdr),0);
  send(sockfd,args,hdr.size,0);

  recvn(sockfd,&hdr,sizeof(hdr),0);
  char buffer[hdr.size];
  recvn(sockfd,buffer,hdr.size,0);

  if (strcmp(buffer,args))
    {
    printf("error: '%s != %s'\n",buffer,args);
    }
  else
    {
    printf("worked: '%s'\n",buffer);
    }
  
  // rem = rcmd(&host, sp->s_port, pw->pw_name, user, args, &rfd2);
  
  free(args);
  close(sockfd);  
  
  return 0;
  }

void server_status(server_thread_args_t *client)
  {
  sendhdr_t hdr = client->hdr;
  int sock = client->sock;

  hdr.kind = DK_status;

  int mypid = getpid();
  statusinfomsg_t *si = get_myhost_status(mypid);
  hdr.size = sizeof(statusinfomsg_t) +
                    si->nrs * sizeof(running_stats_t);

  // x idle - proj/src/xprintidle

  send_response(sock,&hdr,si);
  close(sock);
  }

char *formatuptime(char *p,time_t upt)
  {
  char *buf = p;
  
  time_t minsecs = 60;
  time_t hrsecs = 60 * minsecs;
  time_t daysecs = 24 * hrsecs;
  time_t wksecs = 7 * daysecs;
  
  time_t weeks = upt/wksecs;  upt -= weeks * wksecs;
  time_t days  = upt/daysecs; upt -= days * daysecs;
  time_t hours = upt/hrsecs;  upt -= hours * hrsecs;
  time_t mins  = upt/minsecs; upt -= mins * minsecs;

  if (weeks) { sprintf(p,"%dw,",weeks); while (*p) p++; }
  if (days)  { sprintf(p,"%dd,",days); while (*p) p++; }
  if (hours) { sprintf(p,"%02d:",hours); while (*p) p++; }
  sprintf(p,"%02d:",mins); while (*p) p++;
  sprintf(p,"%02d",upt); while (*p) p++;
  
  return p;
  }

char *formatdrift(char *p,time_t here,time_t there)
  {
  if (here==there) strcpy(p," 0s-drift");
  else if (here>there) sprintf(p," %llds-behind",here-there);
  else sprintf(p," %llds-ahead",there-here);
  while (*p) p++;
  return p;
  }


int print_status(conf_t *conf,const char *hostname,void *showjobs)
  {
  statusinfomsg_t *si = get_host_status(hostname,1);
  if (! si)
    {
    printf("%s: broken\n",hostname);
    return 0;
    }
  
  time_t thistime = time(NULL);
  running_stats_t *r = si->runs;

  int qproc = 0, qrun = 0, qthreads;
  uint64_t qvsize = 0;
  for (int i=1;i<si->nrs;i++)
    {
    qproc    += r[i].proc;
    qrun     += r[i].running;
    qthreads += r[i].threads;
    qvsize   += r[i].vsize;
    }
  
  printf("%s: %d/%dcores %dmips lavg: %d %d %d xproc %d/%d/%d q %d/%d/%d %d ",
         hostname,
         si->info.threads,si->info.cores,si->info.bogomips,
         si->stat.la1,si->stat.la5,si->stat.la15,
         r[0].proc,r[0].running,r[0].threads,
         qproc,qrun,qthreads,
         si->nrs-1);
  printf("mem %dgb used %d avail %d x %lldgb q %lldgb ",
         (si->info.memory+512)/1024,
         (si->stat.memused+512)/1024,
         (si->stat.memavail+512)/1024,
         r[0].vsize>>30,
         qvsize>>30);

  char buf[80];
  formatdrift(formatuptime(buf,si->stat.uptime),
              thistime,si->stat.time);
  printf("users %d %dsec up %s\n",
         si->user.users,si->user.activity,buf);

  if (!showjobs) return 0;

  uid_t myuid = getuid();
  for (int i=1;i<si->nrs;i++)
    {
    uidlink_t *ul = find_or_make_uid(r[i].uid);
    fprintf(stdout," - %s %s %d %d %ld",
           ul->name, strrchr(r[i].jd,'/')+1,
           r[i].proc,r[i].running,r[i].threads,r[i].vsize>>30);
    if (myuid==0 || r[i].uid == myuid)
      {
      parsed_cmd_t pc;
      sendhdr_t *hdr=get_job_cmd(r[i].jd,&pc);
      if (hdr)
        {
        fputc(' ',stdout);
        fputs(pc.cmd,stdout);
        free(pc.env);
        free(hdr);
        }
      }
    
    fputc('\n',stdout);
    }

  return 0;
  }

int status_client(conf_t *conf,int argn,char **argv,char **env)
  {
  char *rhostname = 0;
  char *showjobs = 0;
  int interval,count=0;
  
  char *p = 0;
  
  if (argn>0 && (p = strmatch(argv[0],"show=")))
    {
    if (strmatch(p,"jobs")) showjobs++;
    argn--;
    argv++;
    }
  
  if (argn>0 && (p = strmatch(argv[0],"poll=")))
    {
    int conv = sscanf(p,"%d,%d",&interval,&count);
    printf("here %d %d,%d\n",conv,interval,count);
    if (conv==1) count= -1;
    
    argn--;
    argv++;
    }
  
  if (argn>0)
    {
    rhostname = *argv++;
    argn--;
    }
  else
    rhostname="all";

  while (count)
    {
    for_each_host(conf,rhostname,print_status,showjobs);
    while (sleep(interval));
    count = count>0 ? count -- : count;
    }
    
  return for_each_host(conf,rhostname,print_status,showjobs);
  }

