#include "q.h"

#include "dlc_option.h"
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/wait.h>

/* for this to work need to firwall-config allow tcp
   ports 9090 and 32768-60999.  Actually, you can find
   the port range via:

      cat /proc/sys/net/ipv4/ip_local_port_range
*/

optdes options[] =
    {
    { "h"         ,"help",NULL },
    { "n"         ,"prefix returned std(in|out) with hostname",NULL},
    { "sin"       ,"buffer and send stdin to all hosts",NULL},
    { "p=%s"      ,"port server accepts connections on", "9090" },
    { "daemon"    ,"become host job server as a daemon",NULL },
    { "logfile=%s","log file", NULL },
    { "server"    ,"become host job server in foreground",NULL },
    { "master=%s" ,"network queue server hostname",NULL },
    { "restart=%s","server restart on hostname/group",NULL },
    { "term=%s"   ,"server terminate on hostname/group",NULL },
    { "e"         ,"enqueue job",NULL },
    { "l"         ,"list queue",NULL },
    { "t"         ,"list tokens",NULL },
    { "d"         ,"delete job",NULL },
    { "s"         ,"probe status",NULL },
    { NULL, NULL, NULL },
    };


void print_help(optdes *opt_des,char *name,char *msg)
  {
  fprintf(stderr,"usage: %s %s\n",name,msg);
  fprintf_option_table(stderr,opt_des);
  fprintf(stderr,
          "\nrunnow: [parm=value] command [args]\n"
          "    parms               description\n"
          "    host=name           run on a host or group of hosts\n"
          "\nenqueue: -e [parm=value] command [args]\n"
          "    parms               description\n"
          "    group=host_group    machines to run on\n"
          "    keep=(error,always) when not to delete the run dir\n"
          "    mem=KBytes          memory needed for run\n"
          "    threads=value       threads wanted (0:machine, -1:core)\n"
          "\nlist queue: -l [parm=value]\n"
          "    parms               description\n"
          "    c=command           matching a command\n"
          "    t=time,range        enqueued in a time range\n"
          "    h=host              running on host\n"
          "    j=jobgroup          matching the job group\n"
          "    u=username          matching a user name\n"
          "\ndequeue: -d [same parms as list]\n"
          "\nstatus: -s [parm=value]\n"
          "    parms               description\n"
          "    show=jobs           show jobs running on each host\n"
  );

  exit(1);
  }

void printlog(char *fmt,...)
  {
  char *dmn = dlc_option_value(NULL,"daemon");
  char *elfn = dlc_option_value(NULL,"logfile");
  if (!elfn && dmn) return;
  
  FILE *fp = fopen(elfn,"a");
  if (!fp && dmn) return;

  va_list ap;
  va_start(ap, fmt);

  vfprintf(fp ? fp : stderr, fmt, ap);
  va_end(ap);

  if (fp) fclose(fp);
  }

void server_ctrlchandler (int sig)
  {
  exit(1);  // invoke atexit to remove pidfile
  }


int getserviceport()
  {
  return atoi( dlc_option_value(NULL,"p") );
  }

char *cpyhostname(char *b)
  {
  static struct utsname uni;
  static char *dot;

  if (! dot)
    {
    uname(&uni);
    dot = uni.nodename;
    char c;
    while ( (c=*dot) && c!='.' ) dot++;
    }
  
  return cpystringto(b,uni.nodename,dot);
  }

char *hostname()
  {
  static char dotlesshostname[HOST_NAME_MAX+1];
  if (! dotlesshostname[0])
    {
    cpyhostname(dotlesshostname);
    }
  
  return dotlesshostname;
  }

int childerr(int sock,struct sockaddr_in *addr,socklen_t alen,
             char *msg,int exitcode,datakind_t kind)
  {
  fprintf(stderr,"child error (%08x): %s\n",
          htonl(addr->sin_addr.s_addr),msg);

  if (kind)
    {
    sendhdr_t hdr;
    memset(&hdr,0,sizeof(hdr));
    hdr.kind = kind;
    send_response(sock,&hdr,NULL);
    close(sock);
    }
  
  if (exitcode) exit(exitcode);
  return 0;
  }

void cpydat(int sock,struct sockaddr *addr,socklen_t alen,
            int fd,off_t len)
  {
  while (len)
    {
    char buf[4*1024];
    off_t l = (len>sizeof(buf)) ? sizeof(buf) : len;
    read(fd,buf,l);
    send(sock,buf,l,0);
    len -= l;
    }
  }

void send_response(int sock,sendhdr_t *hdr,const void *data)
  {
  send(sock, hdr , sizeof(*hdr), 0);

  if (data && hdr->size>0)
    send(sock, data, hdr->size, 0);
  
  close(sock);
  }

char *mkdirp(const char *dn,int mode)
  {
  int len = strlen(dn);
  if (len==0) return 0;
  
  const char *dp=dn;
  char *b = alloca(len+1),
       *bp=b;

  while (*dp)
    {
    if (*dp=='/') *bp++ = *dp++;
    while (*dp && *dp == '/') dp++;  // ignore extra /
    
    char *lp=bp;
    while (*dp && *dp!='/') *bp++ = *dp++ ;
    if (bp>lp)
      {
      *bp=0;
      if (mkdir(b,mode) && errno!=EEXIST) return 0;
      }
    }
  return (char *)dn;
  }

#define sprintfa(ma,...) ({  size_t size = snprintf(NULL,0,__VA_ARGS__); \
  char *b = ma(++size);   \
  snprintf(b,size,__VA_ARGS__); \
  b; \
  })


static void systemf(const char *fmt,...)
  {
  va_list ap;
  char buffer[1024];
  char *b=buffer;
  
  va_start(ap,fmt);
  size_t size = vsnprintf(b,sizeof(buffer),fmt,ap);
  va_end(ap);

  
  if (size>sizeof(buffer))
    b = alloca(++size);
  
  va_start(ap,fmt);
  vsnprintf(b,size,fmt,ap);
  va_end(ap);

  system(b);
  }

int server_recv_header(server_thread_args_t *a)
  {
  int master = a->master;
  sendhdr_t *hdr = &a->hdr;
  int sock = a->sock;
  struct sockaddr_in *addr = &a->addr;
  socklen_t alen = a->alen;
  
  size_t size = recvn(sock,hdr,sizeof(*hdr),0);

  if (size<sizeof(*hdr))
    return childerr(sock,addr,alen,"incomplete header",0,DK_trunchdr);
  
  if (bad_magic(hdr->magic))
    return childerr(sock,addr,alen,"bad magic",0,DK_badmagic);
  
  switch (hdr->kind)
    {  // synchronous (main thread) actions
    case DK_terminate:  server_terminate(a); return 0;
    case DK_restart:    server_restart(a);   return 0;
    case DK_syncecho:   server_echo(a);      return 0;
    case DK_getstatus:  server_status(a);    return 0;
    case DK_enqueue:    server_enqueue(a);   return 0; // cl->master
    case DK_jobdone:    server_jobdone(a);   return 0; // server->master
    case DK_dequeue:    server_dequeue(a);   return 0; // cl to master
    case DK_pause  :   // server to master
    default:
      ;
    }
  
  return 1;
  }

// non joinable thread
void *server_thread(void *va)
  {
  server_thread_args_t *a = va;

  switch (a->hdr.kind)
    {
    case DK_echo:       server_echo(a);      return 0;
    case DK_runcmd:     server_run(a);       return 0; // cl to server
    case DK_runjob:     server_runjob(a);    return 0; // master to server
    case DK_killjob:    server_killjob(a);   return 0; // master to server
    case DK_listque:    server_list(a);      return 0; // cl->master
    case DK_listtokens: server_tokens(a);    return 0; // cl->master
    case DK_gettime:    // cl or master to master or server
    case DK_getwho:     // cl to master or server
    case DK_getuptime:  // cl to master or server
    default:
      ;
    }
  return 0;
  }

static inline void enable_autoreaping()
  {
  static int done;
  if (done) return;
  signal(SIGCHLD, SIG_IGN); // enable auto reaping
  done = 1;
  }

int server_restart_req = 0;


void pthread_detached(void *(*start_routine) (void *), void *arg)
  {
  static int inited=0;
  static pthread_attr_t detached;
  
  if (! inited)
    {
    pthread_attr_init(&detached);
    pthread_attr_setdetachstate(&detached, PTHREAD_CREATE_DETACHED);
    inited=1;
    }
    
  pthread_t thread;
  pthread_create(&thread,&detached,start_routine,arg);
  }

void set_pid_dir_from_conf(conf_t *conf)
  {
  conf_t *piddir       = conf_find(conf,"dir","pid",NULL);
  set_pid_dir(piddir->name);
  }

void install_sighandler(int signum, void (*handler)(int))
  {
  struct sigaction sa,old;
  memset(&sa,0,sizeof(sa));
  sa.sa_handler = handler;
  sigemptyset(&sa.sa_mask);
  sigaction(signum,&sa,&old);
  }

void server(conf_t *conf,int argn,char **argv)
  {
  launch_init();

  char *hostn = hostname();
  set_pid_dir_from_conf(conf);

  char *master = dlc_option_value(NULL,"master");  
  int iammaster = ! strcmp(master,hostn);
  if (iammaster) build_token_table(conf);
  
  create_pid_file(hostn);

  install_sighandler(SIGINT,server_ctrlchandler);  
  
  //printlog("%s\n",__FUNCTION__);
  int server_sockfd = open_server_socket( getserviceport() );
  
  // enable_autoreaping();
  
  while(1)
    {  // handle incoming connections
    server_thread_args_t client;
    memset(&client,0,sizeof(client));

    client.conf   = conf;
    client.master = iammaster;
    client.alen = sizeof(client.addr);
    
    client.sock = accept(server_sockfd,
                         (struct sockaddr *)&client.addr,
                         &client.alen);
    // fprintf(stderr,"client.sock %d\n",client.sock);
    
    if (client.sock<1) { if (0) perror("accept(q)"); continue; }

    int sync_task_done = !server_recv_header(&client);
    if (0) printlog("srh %d %d\n",sync_task_done,client.hdr.kind);
    if (sync_task_done)
      { // synchronous tasks are handled already
      if (server_restart_req)
        {
        close(server_sockfd);
        exit(server_restart_req-1);
        }
      
      continue;
      }

    // have an asynchronous task -- create a pthread
    server_thread_args_t *arg = malloc(sizeof(*arg));
    *arg = client;
    pthread_detached(server_thread,arg);
    }

  launch_cleanup();
  exit(0);
  }

void daemonize(conf_t *conf,int argn,char **argv,int ra)
  {
  pid_t pid = fork();
  if (pid) exit(0);  // get rid of parent

  setsid();

  signal(SIGCHLD, SIG_IGN);
  signal(SIGHUP, SIG_IGN);

  pid = fork();
  if (pid) exit(0);  // get rid of parent
  
  chdir("/");

  for (int fd = sysconf(_SC_OPEN_MAX); fd>=0; fd--)
    close (fd);

  signal(SIGCHLD, SIG_DFL);
  pid = fork();
  if (pid == 0) server(conf,argn-ra,argv+ra);
  
  // parent waits for child exit / runs itself 
  int status=0;
  pid_t wpid = wait(&status);
  
  if (wpid<0) exit(1);
  
  if (status) exit(status);
  
  execv(argv[0],argv);
  }

int for_each_host(conf_t *conf,
                  const char *hostname,
                  feh_func_t *f,void *p)
  {
  conf_t *group = conf_find(conf,"group",hostname,NULL);
  if (group)
    { // process all hosts in group
    for (; group ; group = group->next)
      {
      int rv = (*f)(conf,group->name,p);
      if (rv) return rv;
      }
    return 0;
    }

  // search group all for the named host
  if (conf_find(conf,"group","all",hostname,NULL))
    { 
    return (*f)(conf,hostname,p);
    }

  fprintf(stderr,
          "%s: is not a group nor a host in group all\n",
          hostname);
  return 1;
  }

int client(conf_t *conf,char *prog,
            int argn,char **argv,char **env)
  {
  static struct { char *op; client_func_t *f; } modes[] = 
    { "e"       , enqueue_client   ,
      "d"       , dequeue_client   ,
      "l"       , listqueue_client ,
      "t"       , listtokens_client ,
      "s"       , status_client    ,
      "restart" , restart_client   ,
      "term"    , terminate_client };

  const int nmodes = sizeof(modes)/sizeof(modes[0]);
  int i;

  set_pid_dir_from_conf(conf);
  
  for (i=0;i<nmodes;i++)
    if (dlc_option_value(NULL,modes[i].op))
      return modes[i].f(conf,argn,argv,env);

  return run_immediate(conf,prog,argn,argv,env);
  }

char *getqueuemasterhostname()
  {
  return dlc_option_value(NULL,"master");
  }

int main(int argn,char **argv,char **env)
  {
  conf_t *conf = read_conf_file("/etc/queued.conf");
  if (!conf)
    {
    fprintf(stderr,"cant open conf file\n");
    exit(1);
    }

  conf_t *group_master = conf_find(conf,"group","master",NULL);
  
  if (group_master)
    dlc_option_set_default(options,"master",group_master->name);
    
  conf_t *port_service = conf_find(conf,"port","service",NULL);
  if (port_service)
    dlc_option_set_default(options,"p",port_service->name);

  init_env_stuff(env);
  
  int ra=dlc_parse_args(options,argn,argv);

  if (dlc_option_value(NULL,"daemon"))  daemonize(conf,argn,argv,ra);
  if (dlc_option_value(NULL,"server"))  server(conf,argn-ra,argv+ra);

  return client(conf,argv[0],argn-ra,argv+ra,env);
  }

