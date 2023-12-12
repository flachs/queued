
#define _GNU_SOURCE

#include "q.h"

#include <sys/types.h>
#include <pwd.h>
#include <time.h>
#include <sys/wait.h>

const char *queuedd=".queued";
const char *slash="/";
const sendhdr_t hdr_zero;


static char *make_jobdir(conf_t *conf,char *lhostname,
                         uid_t uid,gid_t gid,
                         int argn,char **argv,char **env)
  {
  // find the command
  int acmd;
  for (acmd=0;acmd<argn;acmd++)
    if (! strchr(argv[acmd],'=')) break;

  if (acmd>=argn)
    {
    fprintf(stderr,"no command found\n");
    return 0;
    }
  
  // find out ~
  struct passwd pwent,*pwresult;
  int bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (bufsize<0) bufsize = 16 K;
  
  char buf[bufsize];
  getpwuid_r(uid,&pwent,buf,bufsize,&pwresult);

  // create ~/.queued directory
  const char *xtemp = "XXXXXX";
  
  size_t strlen_lhostname = strlen(lhostname);
  size_t strlen_xtemp = 6;
  
  char queued_dir[strlen(pwent.pw_dir)+1+
                  strlen(queuedd)+1];
  char *fpqueuedd = cpystring(queued_dir,pwent.pw_dir);
  fpqueuedd = cpystring(fpqueuedd, slash);
  fpqueuedd = cpystring(fpqueuedd, queuedd);
  if ( mkdir(queued_dir,0700))
    { // something went wrong
    if (errno!=EEXIST)
      {
      fprintf(stderr,"cant create queue dir %s\n",queued_dir);
      return 0;
      }
    }

  // mkdir happened - allow user to own it
  chown(queued_dir,uid,gid);
  
  // create job directory
  char *jobid = cpystring(fpqueuedd, slash);
  char *jobdir = cpystring(jobid, lhostname);
  jobdir = cpystring(jobdir, xtemp);

  char *jobdirp = mkdtemp(queued_dir);
  if (!jobdirp)
    { // something went wrong
    fprintf(stderr,"cant create job dir %s\n",queued_dir);
    return 0;
    }

  // allow user the job dir.
  chown(queued_dir,uid,gid);

  int fd_jobdir = open(jobdirp,O_RDONLY|O_DIRECTORY|O_PATH);
  if (fd_jobdir < 0) 
    { // something went wrong
    fprintf(stderr,"cant find job dir %s\n",jobdirp);
    return 0;
    }
                       
  // create job files
  // the parm file
  if (acmd)
    {
    int parmsize = calc_arg_size(acmd,argv);
    
    char parmbuf[parmsize],*b=parmbuf;
    for (int i=0;i<acmd;i++)
      b = cpystring(b,argv[i])+1; // cp parms

    int fd_parm = openat(fd_jobdir,"parm",O_WRONLY|O_CREAT,0666);
    if ( fd_parm<0 )
      {
      fprintf(stderr,"cant create parm %s\n",queued_dir);
      close(fd_jobdir);
      return 0;
      }
  
    write(fd_parm,parmbuf,parmsize);
    close(fd_parm);
    }

  // open the stdin file
  if (dlc_option_value(NULL,"sin"))
    {
    int fd_sin = openat(fd_jobdir,"sin",O_WRONLY|O_CREAT,0666);
    if (fd_sin < 0)
      {
      fprintf(stderr,"cant create sin %s\n",queued_dir);
      close(fd_jobdir);
      return 0;
      }

    // copy stdin
    int c;
    char buf[32*1024],*pe = buf+sizeof(buf),*p=buf;
    while ( (c=getchar()) != EOF )
      {
      *p++ = c;
      if (p==pe)
        write(fd_sin,p=buf,sizeof(buf));
      }
    if (p>buf)
      write(fd_sin,buf,p-buf);
    close(fd_sin);
    }

  // build job control file xxx->cmd
  sendhdr_t hdr;
  hdr.magic = 0;
  hdr.uid   = uid;
  hdr.gid   = gid;
  hdr.kind  = DK_runcmd;

  char *cwd = run_client_calc_env_size(&hdr,argn-acmd,argv+acmd,env);
  
  char buffer[hdr.size];
  run_client_build_env_string(buffer,argn-acmd,argv+acmd,env,cwd);
  free(cwd);

  int fd_cmd = openat(fd_jobdir,"xxx",O_WRONLY|O_CREAT,0666);
  if (fd_cmd<0)
    {
    fprintf(stderr,"cant create xxx %s\n",queued_dir);
    close(fd_jobdir);
    return 0;
    }

  write(fd_cmd,&hdr,sizeof(hdr));
  write(fd_cmd,buffer,hdr.size);
  close(fd_cmd);

  if (renameat(fd_jobdir,"xxx",fd_jobdir,"cmd"))
    {
    fprintf(stderr,"cant rename xxx -> cmd %s\n",queued_dir);
    close(fd_jobdir);
    return 0;
    }
  
  close(fd_jobdir);
  
  size_t lenrv = strlen_lhostname+strlen_xtemp;
  char *rv = malloc(lenrv+1);
  strncpy(rv,jobid,lenrv);
  rv[lenrv]=0;
  
  return rv;
  }

int enqueue_client(conf_t *conf,int argn,char **argv,char **env)
  {
  int port = getserviceport();
  char *qmhn = getqueuemasterhostname();
  char *lhostname = hostname();

  int bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (bufsize<0) bufsize = 16 K;
  
  char buf[bufsize];
  uid_t uid = getuid();
  gid_t gid = getgid();

  int jg = 0;
  char *jgp=0;
  if (argn>1 && (jgp = strmatch(argv[0],"jg=")))
    {
    jg = atoi(jgp);
    argn --;
    argv ++;
    }

  if (argn<=0)
    {
    fprintf(stderr,"no command to enqueue\n");
    exit(1);
    }
  
  char *jname = make_jobdir(conf,lhostname,uid,gid,argn,argv,env);
  
  if (! jname) return 1;
  size_t jsize = strlen(jname);
  
  printf("jobid: %s %d %d\n",jname,uid,gid);

  sendhdr_t hdr,rdr;
  hdr.uid   = uid;
  hdr.gid   = gid;
  hdr.kind  = DK_enqueue;    // queue.c: server_enqueue
  hdr.size  = jsize+1;
  hdr.value[0] = jg;
  rdr.kind  = DK_reject;

  int count;
  for (count=0;count<3;count++)
    {
    if ( get_magic_for_host(qmhn,& hdr.magic) ) return 1 ;
  
    // notify server of new job
    int sockfd = open_client_socket(qmhn,port,"master");
    if (sockfd<0) return 1;  
  
    send(sockfd,&hdr,sizeof(hdr),0);
    send(sockfd,jname,jsize+1,0);

    // recieve confirmation
    recvn(sockfd,&rdr,sizeof(hdr),0);

    char buffer[rdr.size+1];
    recvn(sockfd,buffer,rdr.size,0);
    close(sockfd);

    // check if server accepted
    if (rdr.kind == DK_reply)
      {
      printf("sch %d\n",rdr.value[0]);
      return 0;
      }

    // maybe NFS was slow to show job dir -- try again
    while (sleep(3));
    }
  fprintf(stderr,"cant schedule job\n");
  return 1;
  }

uid_t determine_uid(char *un,uid_t uid)
  {
  if (un[0] == '-') return -1;
  else
    {
    uidlink_t *ul = find_uid_by_name(un);
    if (ul) return ul->uid;
    }
  return uid;
  }

void parse_args_jms(job_match_spec_t *jms,int argn,char **argv)
  {
  int argi=0;
  for (; argi<argn ; argi++)
    {
    char *eq = strchr(argv[argi],'=');
    if (eq) 
      {
      char o = argv[argi][0];
      if (o=='t')
        { // time range
        jms->spec = JMS_TIME;
        char *tos   = eq+1;
        char *comma = strchr(tos,',');
        if (comma>tos)
          { // time range
          *comma = 0;
          jms->beg = parse_time(tos);
          jms->end = parse_time(comma+1);
          }
        else if (comma == tos)
          { // the end time
          jms->end = parse_time(tos+1);
          }
        else
          { // the beg time
          jms->beg = parse_time(tos);
          }        
        }
      else if (o=='c')
        { // cmd re
        jms->spec |= JMS_CMDSS;
        strcpy(jms->value,eq+1);
        }
      else if (o=='h')
        { // host
        jms->spec |= JMS_HOST;
        strcpy(jms->value,eq+1);
        }
      else if (o=='j' && argi+1<argn)
        { // job group
        jms->spec |= JMS_JG;
        jms->jg = atoi(eq+1);
        }
      else if (o=='a')
        { // select all jobs
        jms->spec |= JMS_ALL;
        }
      else if (o=='u')
        { // user name
        jms->spec |= JMS_USER;
        char *name = eq+1;
        jms->uid = determine_uid(name,jms->uid);
        }
      }
    else
      {
      jms->spec |= JMS_JOBDIR;
      strcpy(jms->value,argv[argi]);
      }
    }
  
  if (jms->spec==0) jms->spec = JMS_ALL;
  }

int queue_client(conf_t *conf,
                 int argn,char **argv,char **env,
                 char *mesg,
                 datakind_t kind)
  {
  sendhdr_t hdr = hdr_zero;
  hdr.kind = kind;  
  
  hdr.size = sizeof(job_match_spec_t);
  for (int argi=0;argi<argn;argi++)
    hdr.size += strlen(argv[argi])+1;

  char buf[hdr.size];
  memset(buf,0,hdr.size);
  
  job_match_spec_t *jms=(job_match_spec_t *)buf;
  memset(buf,0,hdr.size);
  
  jms->uid  = getuid();
  
  jms->beg  = (time_t)1<<(sizeof(time_t)*8-1);
  jms->end  = jms->beg-1;

  parse_args_jms(jms,argn,argv);
  
  char *s = simple_request(getqueuemasterhostname(),
                           mesg,
                           &hdr,buf);

  if (hdr.size)
    {
    fputs(s,stdout);
    free(s);
    }
  
  return 1;
  }

int listqueue_client(conf_t *conf,int argn,char **argv,char **env)
  {
  return queue_client(conf,argn,argv,env,
                      "listqueue",DK_listque);// queue.c: server_lsq
  }


int dequeue_client(conf_t *conf,int argn,char **argv,char **env)
  {
  return queue_client(conf,argn,argv,env,
                      "dequeue",DK_dequeue);// dequeue.c: server_dequeue
  }
