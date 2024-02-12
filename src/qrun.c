#include "q.h"

/* this file implements both the client and the server side of a
   remote shell command operation.  There is no remote login
   equivalent.  The argument parsing part of the client is now
   in runimm.c.  It calls either run_remotehost or run_localhost.

   the basic formulation is:
   
   q host=hostname cmd args < inputdata > resultdata

   which runs cmd with args on the specified host providing data to
   standard in and saveing data from stdout.  stdout is also
   transported back to the user.

   A couple of options are available...

   -n prefixes each line from stdout with hostname: of the host
      that produces the output.

   -sin duplicates stdin to give each host a copy of stdin.
        assumes stdin is small enough to be reasonably stored.

   These options are intented to improve the usefulness of
   
   q host=hostgroup cmd args

   which runs cmd and args on every host in the group.  and the
   simpler version

   q cmd args

   runs cmd and args on every host in the all group.

*/

#include <signal.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <sys/wait.h>
#include <stdarg.h>

typedef struct
  {
  int sock,io_sock,se_sock;
  char *cmd,*wdir,**env;
  } server_run_state_t;

/* if -sin is enabled, need to read and buffer entire stdin stream.
   dont know how big to make a buffer, so the approach is based upon
   realloc.

   otherwise need to read chunks from stream as available and write
   out as needed in circular fashion */

typedef struct
  {
  char *wrp,*rdp;  // write and read pointers
  size_t size,     // amount in buffers
         load,     // how much data is ready for reading
         room,     // how much room is left for filing
         done;     // data source is done
  char buf[0];
  } cq_string;

// return a pointer to end of buffer
static inline char *cq_string_eob(cq_string *cq)
  {
  return cq->buf + cq->size;
  }

// return a pointer to beginning of buffer
static inline char *cq_string_buf(cq_string *cq)
  {
  return cq->buf ;
  }

// initialize buffer to have a size and be empty
static inline cq_string *init_cq_string(cq_string *cq,size_t size)
  {
  cq->wrp = cq->rdp = &(cq->buf[0]);
  cq->size = cq->room = size;
  cq->load = 0;
  return cq;
  }

// total sizeof buffer of a certain capacity
static inline size_t cq_string_space(size_t size)
  {
  return size+sizeof(cq_string);
  }

// malloc and init a buffer
static inline cq_string *malloc_cq_string(int size)
  {
  return init_cq_string( malloc(cq_string_space(size)), size );
  }

// alloc from stack and init a buffer
#define alloc_cq_string(s) init_cq_string(alloca(cq_string_space(s)), s)

// read from fd and fill a buffer, or if already filled
// reset to already filled but never read state
static cq_string *read_it_all(cq_string **scq,int fd)
  {
  int chunk=4 K;
  cq_string *cq = scq ? *scq : 0;
  if (cq)
    { // already created, prepare to re-read it
    cq->rdp = cq->buf;
    cq->load = cq->size-chunk;
    cq->room = chunk;
    return cq;
    }
  
  int tot=0,bytes=0;

  do
    {
    tot += bytes;
    cq = realloc(cq,sizeof(cq_string) + tot + chunk);
    bytes = read(fd,cq->buf + tot, chunk);
    }
  while (bytes>0);
  cq->wrp = cq->buf+tot;
  cq->rdp = cq->buf;
  cq->size = tot+chunk;
  cq->load = tot;
  cq->room = chunk;
  cq->done = 1;

  if (scq) *scq = cq;
  return cq;
  }

// read from fd into the buffer, 
// unless fd is done already
// read to wrp to either the end of buffer or according to
// room left.
static inline ssize_t read_into_cq_string(cq_string *cq,int fd)
  {
  size_t room = min( cq_string_eob(cq) - cq->wrp , cq->room);

  if (cq->done)
    {
    fprintf(stderr,"stdin empty\n");
    return 0;
    }
  
  //fprintf(stderr,"getting from stdin %d\n",room);
  ssize_t bytes = read(fd,cq->wrp,room);

  if (bytes>0)
    {
    cq->wrp += bytes;
    if (cq->wrp==cq_string_eob(cq)) cq->wrp = cq_string_buf(cq);
    cq->room -= bytes;
    cq->load += bytes;
    }
  else
    cq->done = 1;
  
  return bytes;
  }

// send to fd from the buffer at rdp until either end of buffer
// or how much data is loaded.
static inline ssize_t send_from_cq_string(cq_string *cq,int fd)
  {
  size_t stuf = min(cq->load,cq_string_eob(cq) - cq->rdp);
  ssize_t bytes = send(fd,cq->rdp,stuf,MSG_DONTWAIT);
  //fprintf(stderr,"sending to stdin soc %d\n",bytes);

  if (bytes>0)
    {
    cq->rdp += bytes;
    if (cq->rdp==cq_string_eob(cq)) cq->rdp = cq_string_buf(cq);
    cq->room += bytes;
    cq->load -= bytes;
    }

  return bytes;
  }

// connect back to client to transfer data streams.
int connect_run_client_socket(server_thread_args_t *client,
                              int port,
                              uint64_t magic)
  {
  sendhdr_t *hdr   = & client->hdr;
  sendhdr_t init_hdr = *hdr;
  init_hdr.magic = magic;
  init_hdr.kind = 0;
  init_hdr.size = 0;
  init_hdr.value[0] = 0;
  init_hdr.value[1] = 0;
  
  struct sockaddr_in *sa = (struct sockaddr_in *)&(client->addr);
  in_addr_t hostaddr = sa->sin_addr.s_addr;

  int sock = connected_socket(hostaddr,port);
  //fprintf(stderr,"sock %d\n",sock);

  send(sock,&init_hdr,sizeof(init_hdr),0);
  return sock;
  }

// replace nul between args with space 
char *glue_cmd_spaces(char *cmd,int argn)
  {
  int amm = argn;
  while (1)
    {
    if (*cmd==0)
      {
      amm--;
      if (amm==0) break;
      *cmd = ' ';
      }
    cmd++;
    }
  return cmd+1;
  }

// create and fill a list of pointers to the environment
// variable definitions
void *malloc_env_list(char *envp,int nenviro)
  {
  char **enviro = calloc(nenviro+1,sizeof(*enviro));
  
  for (int i=0;i<nenviro;i++)
    {
    // fprintf(stderr,"env %d = %s\n",i,envp);
    enviro[i]  = envp;
    envp += strlen(envp) + 1;
    }
  return enviro;
  }

// setup the surroundings of a remotely running process
// based upon the data sent in the run request
server_run_state_t *server_run_init(server_thread_args_t *client)
  {
  sendhdr_t *hdr = & client->hdr;
  int sock       = client->sock;
  int stuffsize  = hdr->size;

  if (! stuffsize)
    {
    fprintf(stderr,"got no stuff\n");
    close(sock);
    return NULL;
    }

  int argn        = hdr->value[0];
  int nenviro     = hdr->value[1];
  int io_port     = hdr->value[2];
  int se_port     = hdr->value[3];
  uint64_t magic  = get_tag_ui32x2_ui64(&hdr->value[4]);
  
  // open stdio sock
  int io_sock = -1;
  if (io_port)
    {
    io_sock = connect_run_client_socket(client,io_port,magic);
    if (io_sock<0)
      {
      close(sock);
      return NULL;
      }
    }
  
  // open stderr sock
  int se_sock = -1;
  if (se_port)
    {
    se_sock = connect_run_client_socket(client,se_port,magic);
    if (se_sock<0)
      {
      close(io_sock);
      close(sock);
      return NULL;
      }
    }

  // receive command, cwd, env
  char *stuff = malloc(stuffsize);
  size_t stuffbytesr = recvn(sock,stuff,stuffsize,0);

  if (stuffbytesr<stuffsize)
    {
    fprintf(stderr,"got NO stuff\n");
    free(stuff);
    if (se_sock<0) close(se_sock);
    if (io_sock<0) close(io_sock);
    close(sock);
    return NULL;
    }

  // prepare to return the state
  server_run_state_t *ss = malloc(sizeof(*ss));
  ss->sock = sock;
  ss->se_sock = se_sock;
  ss->io_sock = io_sock;
  
  // save pointers to the command, the working dir
  ss->cmd = stuff;
  ss->wdir = glue_cmd_spaces(ss->cmd,argn);

  // the env list
  char *envp = ss->wdir + strlen(ss->wdir)+1;
  ss->env = malloc_env_list(envp,nenviro);
  
  // printf("server: %s / %s\n",ss->wdir,ss->cmd);
  return ss;
  }

// prepare a one of the stdin, stdout, stderr streams to
// get or take their data from files in the job dir
void setup_stream(int dfd,int fd,
                  const char *jd,  // job dir
                  const char *fn,  // file in job dir
                  int flags)
  {
  if (fd>=0)
    {
    dup2(fd, dfd);
    return;
    }
  else
    {
    fd = openjob(jd,fn,flags,NULL);
    if (fd<0) return;

    dup2(fd, dfd);
    close(fd);
    return;
    }
  }

/* server_child_fork is used both by:
 * server_run for immediate job execution (jd=NULL) and
 * launchcontrol for queued job execution (jd is valid)
 * eventually execs the user's desired job
 */
int server_child_fork(uid_t uid,gid_t gid,
                      int csfd,int sifd,int sofd,int sefd,
                      char *jd,
                      char *wdir,char *cmd,char **env)
  {
  int childpid = fork();
  if (childpid) return(childpid);
  
  // child
  /* Become a process group leader, so that
   * the control process above can send signals to all the
   * processes we may be the parent of.  The process group ID
   * (the getpid() value below) equals the childpid value from
   * the fork above.
   */

  pid_t mypid=getpid();

  setpgid(0, mypid);

  // become the user & groups
  int bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (bufsize<0) bufsize = 16 K;
  
  char buf[bufsize];
  struct passwd pwent,*pwresult;
  getpwuid_r(uid,&pwent,buf,bufsize,&pwresult);
  setgid(gid);
  initgroups(pwent.pw_name, gid);	
  setuid(uid);

  if (jd)
    {
    int mfd = openjob(jd,"machine",O_WRONLY|O_CREAT,NULL);
    if (mfd)
      {
      dprintf(mfd,"%s %d\n",hostname(),mypid);
      close(mfd);
      }
    }
  
  // cd to the right dir
  chdir(wdir);

  if (csfd>=0) close(csfd);    /* parent/control process handles this fd */

  // setup std streams 
  setup_stream(STDIN_FILENO ,sifd,jd,"sin" ,O_RDONLY);
  setup_stream(STDOUT_FILENO,sofd,jd,"sout",O_WRONLY|O_CREAT);
  setup_stream(STDERR_FILENO,sefd,jd,"serr",O_WRONLY|O_CREAT);

  // note closes are after stream setups since maybe sifd == sofd 
  if (sifd>=0) close(sifd);
  if (sofd>=0) close(sofd);
  if (sefd>=0) close(sefd);
  
  // Set up an initial environment for the shell that we exec().
  char *cp = strrchr(pwent.pw_shell, '/');
  if ( cp != NULL) cp++;        /* step past first slash */
  else cp = pwent.pw_shell;	/* no slash in shell string */

  // only returns if error happens
  execle(pwent.pw_shell, cp, "-fc", cmd, NULL, env);

  perror(pwent.pw_shell);
  exit(1);  // exit the child after an exec error
  }


/* server forks a job (maybe multiple jobs) and needs to know when
   the jobs exit.
   Before a fork, space is allocated in push_pid_done with a 0 pid value.
   When a child exits it causes a SIGCHLD, handled by run_server_sighandler
   which waits, gets a pid, finds a pid=0 entry and puts the pid
   and exit value in the entry.
   after the fork, the server can stand in a select waiting for
   something to happen either io or a pid_done event.  It can
   search through the pid_done's and if it finds the child it was
   waiting for send exit status to the client. */
typedef struct
  {
  pid_t pid;
  int status;
  } pid_element_t;

pid_element_t *pid_done_queue;
int pid_done_queue_n,
  pid_done_queue_length,
  pid_done_queue_size;

void push_pid_done_queue()
  {
  pid_done_queue_n++;
  if (pid_done_queue_n<=pid_done_queue_length) return;
  
  pid_done_queue_length++;
  if (pid_done_queue_length>pid_done_queue_size)
    {
    if (pid_done_queue_size==0)
      {
      int init_size = 128;
      pid_done_queue = malloc(sizeof(pid_element_t)*init_size);
      pid_done_queue_size = init_size;
      }
    else
      {
      pid_done_queue_size <<= 1;
      pid_done_queue = realloc(pid_done_queue,
                               sizeof(pid_element_t)*pid_done_queue_size);
      }
    }

  pid_done_queue[pid_done_queue_length-1].pid = 0;
  pid_done_queue[pid_done_queue_length-1].status = 0;
  }

void run_server_sighandler(int sig, siginfo_t *info, void *ucontext)
  { // child has exited
  int status;
  pid_t childpid;
  int saved_errno = errno;
  
  while ( (childpid = waitpid((pid_t)(-1), &status, WNOHANG) ) > 0)
    {
    for (int i=0;i<pid_done_queue_length;i++)
      {
      if (pid_done_queue[i].pid == 0) 
        {
        pid_done_queue[ i ].pid    = childpid;
        pid_done_queue[ i ].status = status;
        errno = saved_errno;
        return;
        }
      }
    }
  errno = saved_errno;
  }

int pid_done(pid_t pid,int *status)
  {
  for (int i=0;i<pid_done_queue_length;i++)
    {
    if (pid_done_queue[i].pid == pid) 
      {
      if (status) *status = pid_done_queue[ i ].status;
      pid_done_queue[ i ].pid    = 0;
      pid_done_queue_n --;
      return 1;
      }
    }
  return 0;
  }

// recieve and run a remote job
void server_run(server_thread_args_t *client)
  { // on a parallel thread in the server process space
  
  // setup signal handler for child exits
  struct sigaction sigint_action,sigint_old_action;
  sigint_action.sa_handler = NULL;
  sigint_action.sa_sigaction = run_server_sighandler;
  sigemptyset(&sigint_action.sa_mask);
  sigint_action.sa_flags = SA_SIGINFO;

  // install signal handler
  if (sigaction(SIGCHLD, &sigint_action , &sigint_old_action))
    { // can install handler
    close(client->sock);
    return;
    }

  server_run_state_t *ss = server_run_init(client);

  if (!ss) return;  // channels are down
  
  // start sub process
  push_pid_done_queue();
  pid_t childpid=server_child_fork(client->hdr.uid,client->hdr.gid,
                                   ss->sock,
                                   ss->io_sock,ss->io_sock,
                                   ss->se_sock,
                                   NULL,
                                   ss->wdir,ss->cmd,ss->env);
  
  close(ss->io_sock);	// child handles stdio socket 
  close(ss->se_sock);	// child handles stderr socket
  
  // signal/exit status loop
  int nfds = ss->sock + 1;
  while (1)
    {
    int status;
    if (pid_done(childpid,&status))
      {  // child has exited
      char buf[16];
      buf[0] = status;
      send(ss->sock,buf,1,0);
      break;
      }
    
    fd_set readfds, writefds, exceptfds;
    FD_ZERO(&readfds); FD_ZERO(&writefds); FD_ZERO(&exceptfds);

    FD_SET(ss->sock, &readfds);  // incoming signals
    
    int ready = select(nfds,&readfds,&writefds,&exceptfds, NULL);
    
    if (ready == -1 && errno == EINTR) continue;

    if (FD_ISSET(ss->sock, &readfds))
      { // have incoming signal send from client
      char buf[16];
      ssize_t bytes = recv(ss->sock,buf,1,MSG_DONTWAIT);
      int sigval = buf[0];
      
      killpg(childpid, sigval);
      }
    }
  //printlog("exiting\n");
  
  close(ss->sock);
  free(ss->env);
  free(ss->cmd);
  free(ss);
  }

// how bit is the arg string
int calc_arg_size(int argn, char **argv)
  {
  int len = 0;
  int argi = 0;

  for (char **ap = argv; *ap && (argn<0 || (argi<argn)); ap++,argi++)
    {
    int lenit = strlen(*ap);
    len += lenit + 1;
    }
  return(len);
  }

// malloc and build arg string
char *make_arg_string(int argn, char **argv)
  {
  int len = 0;
  for (char **ap = argv; *ap; ap++)
    len += strlen(*ap) + 1;

  char *rv = malloc(len);
  rv[0]=0;
  
  for (char **ap = argv; *ap; ap++)
    {
    strcat(rv, *ap);
    if (ap[1]) strcat(rv," ");
    }
  return(rv);
  }

// how much space to store the env
char *run_client_calc_env_size(sendhdr_t *hdr,
                              int argn,char **argv,char **env)
  {
  int     al = calc_arg_size(argn,argv);

  int size = al+1;
  char *cwd = getcwd(NULL,0);
  int wdl  = strlen(cwd);
  
  size += wdl+1;
  
  int nenv=0;
  for (char **e = env; *e ; e++)
    {
    nenv++;
    int el = strlen(*e);
    size += el+1;  // not dis play
    }
  
  size += HOST_NAME_MAX;  // extra room for max size hostname in DISPLAY
  
  hdr->size = size; 
  hdr->value[0] = argn;
  hdr->value[1] = nenv;
  return cwd;
  }

// search a string for one of a number of prefixes.
// return a pointer to what follows a prefix, or NULL
// if no matches
// list of prefixes is ended by a NULL
char *strmatchany(char *b,...)
  {
  char *rv = 0;
  
  va_list ap;
  
  va_start(ap,b);
  
  while (!rv)
    {
    char *s = va_arg(ap,char *);
    if (! s) break;

    rv = strmatch(b,s);
    }
  
  va_end(ap);
  return rv;
  }

// build an env to send with a command.  mostly copy current
// env, but fix DISPLAY var to point to user's host.
void run_client_build_env_string(char *buffer,
                                 int argn,char **argv,char **env,
                                 char *cwd)
  {
  char *b=buffer,c;

  for (int i=0;i<argn;i++)
    b = cpystring(b,argv[i])+1; // cp args

  b = cpystring(b,cwd)+1;       // cp current dir

  for (char **e = env; *e ; e++)    // cp env
    {
    char *p=*e;
    char *ed,*ec;
    
    if ( (ed = strmatch(p,"DISPLAY=")) &&
         (ec = strmatchany(ed,":","localhost:","unix:",NULL) ) )
      {
      b = cpystringto(b,p,ed);
      b = cpyhostname(b);
      b = cpystring(b,ec-1)+1;
      }
    else
      b = cpystring(b,p)+1;
    }
  }

// open a socket for server to connect stdio/err too
int run_client_open_server_socket(uint32_t *socknum)
  {
  int sockfd = open_server_socket(0);
  *socknum   = get_sock_port(sockfd);
  return sockfd;
  }

// send the command message to server
void run_client_send_init_message(int sockfd,sendhdr_t *hdr,
                                  int buflen,char *buffer)
  {
  send(sockfd,hdr,sizeof(*hdr),0);
  send(sockfd,buffer,buflen,0);
  }

// wait for connection from server for stdio/err
int run_client_wait_for_connect(int sockfd,const char *which)
  {
  // wait for server to connect on sockfd channel
  server_thread_args_t server;
  server.alen = sizeof(server.alen);
  int sock=-1;
  
  sendhdr_t hdr;
  while (1)
    {
    sock = accept(sockfd,
                  (struct sockaddr *)&server.addr,
                  &server.alen);
    if (sock<1) { if (0) perror("accept(qrun)"); continue; }

    size_t size = recvn(sock,&hdr,sizeof(hdr),0);
    if (size<sizeof(hdr) || bad_magic(hdr.magic))
      {
      printf("header sucks\n");
      close(sock);
      sock = -1;
      continue;
      }
    break;
    }

  close(sockfd);  // dont want any more connections
  return sock;
  }

// package up all of the connections to server
typedef struct
  {
  int sio,  // stdin & stdout
      err,  // stderr
      sgc;  // signal delivery & exit status 
  } run_client_connect_info;

// init a connection to the server to run a command
run_client_connect_info run_client_open_server(int wantpipe,
                                               char *hostname,
                                               int argn,char **argv,
                                               char **env)
  {

  
  run_client_connect_info info;
  int port = getserviceport();
  info.sgc = open_client_socket(hostname,port,NULL);
  info.sio = -1;
  info.err = -1;

  if (info.sgc<0)
    {
    fprintf(stderr,"cant connect to server %s\n",hostname);
    return info;
    }
  
  sendhdr_t hdr;
  if (get_magic_for_host(hostname,& hdr.magic))
    {
    fprintf(stderr,"cant get magic for server %s\n",hostname);
    return info;
    }
  
  hdr.uid   = getuid();
  hdr.gid   = getgid();
  hdr.kind  = DK_runcmd;
  
  char *cwd = run_client_calc_env_size(&hdr,argn,argv,env);
  
  char buffer[hdr.size];
  run_client_build_env_string(buffer,argn,argv,env,cwd);
  free(cwd);

  if (wantpipe)
    {
    make_magic((uint64_t *)&hdr.value[4]);
    int sio = run_client_open_server_socket(&hdr.value[2]);
    int err = run_client_open_server_socket(&hdr.value[3]);

    run_client_send_init_message(info.sgc,&hdr,hdr.size,buffer);
    
    info.sio = run_client_wait_for_connect(sio,"sio");
    info.err = run_client_wait_for_connect(err,"err");
    }
  else
    {
    hdr.value[2] = hdr.value[3] = hdr.value[4] = hdr.value[5] = 0;
    run_client_send_init_message(info.sgc,&hdr,hdr.size,buffer);
    }
  
  return info;
  }

// close all open connections
static inline void run_client_close_all(run_client_connect_info *info)
  {
  if (info->sio) close(info->sio);
  if (info->err) close(info->err);
  if (info->sgc) close(info->sgc);
  info->sio = -1;
  info->err = -1;
  info->sgc = -1;
  }

// count the open connections
static inline int run_client_count_open(run_client_connect_info *info)
  {
  int rv=0;
  
  if (info->sio) rv++;
  if (info->err) rv++;
  if (info->sgc) rv++;
  return rv;
  }

// handler to record recieved signals, for delivery to remote cmd
int volatile signal2send = 0;
int volatile signalcount = 0;
void run_client_sighandler(int sig, siginfo_t *info, void *ucontext)
  {
  signal2send = sig;
  signalcount++;
  }

// write a buffer of characters to a stream.
// optionally prefixing the first character of a new line with a prefix
int fputit(char *pre,int bol,char *buf,size_t bytes,FILE *fp)
  {
  char *p = buf;
  while (bytes>0)
    {
    if (pre && bol) { fputs(pre,fp); }
    bol=0;
    
    char c = *p++;
    bytes--;
    fputc(c,fp);
    if (c=='\n') bol=1;
    }
  return bol;
  }

// run a command on a remote host by setting up a connection
// to a remote server
int run_remotehost(int multihost,char *rhostname,
                   int argn,char **argv,char **env)
  {
  int hostprefix = dlc_option_value(NULL,"n") != 0;
  int stdintoall = dlc_option_value(NULL,"sin") != 0;

  // setup to handle signals from user to forward to server
  struct sigaction sigint_action,sigint_old_action;
  sigint_action.sa_handler = NULL;
  sigint_action.sa_sigaction = run_client_sighandler;
  sigemptyset(&sigint_action.sa_mask);
  sigint_action.sa_flags = SA_SIGINFO;

  // install signal handler
  if (sigaction(SIGINT, &sigint_action , &sigint_old_action))
    {
    perror("signal");
    exit(1);
    }

  // open connection to the server
  int wantpipe=1;
  int closesin=wantpipe && multihost && !stdintoall;
  run_client_connect_info srv =
    run_client_open_server(wantpipe,rhostname,argn,argv,env);

  int done = srv.sio<0 || srv.err<0 || srv.sgc<0 ;
  int exit_code= 0, have_exit=0;

  // prefix setup.  streams start at bol
  int bol_stdout = 1;  // prefix beginning of first line
  int bol_stderr = 1;
  char prefix[strlen(rhostname)+3];
  char *pfx_stdout = 0, *pfx_stderr = 0;
  if (hostprefix)
    {
    cpystring(cpystring(prefix,rhostname), ": ");
    pfx_stdout = prefix;
    pfx_stderr = prefix;
    }

  // deal with stdin stream
  int sin_done  = closesin || !wantpipe,
      sout_done = !wantpipe,
      serr_done = !wantpipe,
      sig_done  = 0;
  static cq_string *saved_sin;
  
  if (closesin) shutdown(srv.sio, SHUT_WR);
  cq_string *sib = stdintoall ?
                   read_it_all(&saved_sin,STDIN_FILENO) :
                   alloc_cq_string(16 K);  

  // io loop
  while (! done)
    {
    if (0) printf("client IO loop %d %d %d %d buf=%d\n",
                  have_exit,sout_done,serr_done,sin_done,sib->load);
    
    fd_set readfds, writefds, exceptfds;
    FD_ZERO(&readfds); FD_ZERO(&writefds); FD_ZERO(&exceptfds);

    // read stdin 
    if (!sib->done && sib->room > 1 K)
      {
      FD_SET(STDIN_FILENO, &readfds);
      //fprintf(stderr,"enabling stdin\n");
      }
    
    // write stdout && stderr
    // FD_SET(STDOUT_FILENO, &writefds);
    
    // read from socket for stdout / write to socket for stdin
    if (!sout_done)                FD_SET(srv.sio, &readfds);  //stdout
    if (!sin_done && sib->load>0)  FD_SET(srv.sio, &writefds); //stdin
    if (!serr_done)                FD_SET(srv.err, &readfds);  //stderr 
    
    FD_SET(srv.sgc, &readfds);  // want the return value
    if (!have_exit && !sig_done && signal2send)
      FD_SET(srv.sgc, &writefds); //signal

    int nfds  = max(max(srv.sio,srv.err),srv.sgc) + 1;
    int ready = select(nfds,&readfds,&writefds,&exceptfds, NULL);

    if (ready == -1 && errno == EINTR) goto bottom;

    if (FD_ISSET(STDIN_FILENO, &readfds))
      { // have data ready from stdin
      int bytes = read_into_cq_string(sib,STDIN_FILENO);
      //fprintf(stderr,"getting stdin %d %d\n",bytes,sib->load);
      if (bytes<=0 && sib->load==0) // nothing to send, no more to go
        {
        sin_done=1;
        shutdown(srv.sio, SHUT_WR);
        }
      }
    if (FD_ISSET(srv.sio, &writefds))
      { // room to send stdin data
      ssize_t bytes = send_from_cq_string(sib,srv.sio);
      //fprintf(stderr,"sending stdin %d\n",bytes);
      if (bytes<=0 || (sib->load==0 && sib->done))
        {
        sin_done=1;
        shutdown(srv.sio, SHUT_WR);
        }
      }
    
    if (FD_ISSET(srv.sio, &readfds))
      { // have data ready for stdout
      char buf[16*1024];
      ssize_t bytes = recv(srv.sio,buf,sizeof(buf),MSG_DONTWAIT);
      if (bytes<=0) { sout_done=1 ; shutdown(srv.sio, SHUT_RD); }
      else bol_stdout = fputit(pfx_stdout,bol_stdout,buf,bytes,stdout);
      }

    if (FD_ISSET(srv.err, &readfds))
      { // have data ready for stderr
      char buf[16*1024];
      
      ssize_t bytes = recv(srv.err,buf,sizeof(buf),MSG_DONTWAIT);
      //fprintf(stderr,"getting stderr %d \n",bytes);
      if (bytes<=0) { serr_done = 1; shutdown(srv.err, SHUT_RDWR); }
      else bol_stderr = fputit(pfx_stderr,bol_stderr,buf,bytes,stderr);
      }
    
    if (FD_ISSET(srv.sgc, &readfds))
      { // exit code coming
      char buf[1];
      ssize_t bytes = recv(srv.sgc,buf,1,MSG_DONTWAIT);
      //fprintf(stderr,"getting exit %d \n",bytes);
      shutdown(srv.sgc, SHUT_RDWR);
      have_exit=1;
      if (bytes>0)
        {
        exit_code = 0x255 & buf[0];
        }
      }
    if (!have_exit && FD_ISSET(srv.sgc, &writefds))
      { // room to send signal
      char buf[1];
      buf[0] = signal2send;
      
      ssize_t bytes = send(srv.sgc,buf,1,MSG_DONTWAIT);
      if (bytes<0)
        {
        sig_done = 1;
        shutdown(srv.sgc, SHUT_WR);
        }
      else if (bytes>0)
        {
        signal2send = 0;
        }
      }
  bottom:
    
    done = have_exit && sout_done && serr_done;
    }
  
  run_client_close_all(&srv);

  if (sigaction(SIGINT, &sigint_old_action, NULL))
    {
    perror("signal");
    exit(1);
    }
  
  return (have_exit ? exit_code : 1);
  }


// run the command locally
int run_localhost(int needfork,int argn,char **argv,char **env)
  {
  int cmdlen=0;

  // build command
  for (int i=0;i<argn;i++)
    cmdlen += strlen(argv[i])+1;

  char cmd[cmdlen],*cp=cmd;
  for (int i=0;i<argn;i++)
    {
    cp = cpystring(cp,argv[i]);
    *cp++ = ' ';
    }
  *cp = 0;
  
  int bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (bufsize<0) bufsize = 16 K;
  
  char buf[bufsize];
  struct passwd pwent,*pwresult;
  uid_t uid = getuid();
  getpwuid_r(uid,&pwent,buf,bufsize,&pwresult);

  cp = strrchr(pwent.pw_shell, '/');
  if ( cp != NULL) cp++;        /* step past first slash */
  else cp = pwent.pw_shell;	/* no slash in shell string */

  if (needfork)
    {
    int childpid = fork();
    if (childpid)
      {
      int status;
      wait(&status);
      return status;
      }
    }

  // become proper user
  gid_t gid = getgid();
  setgid(gid);
  initgroups(pwent.pw_name, gid);	
  setuid(uid);

  
  // only returns if error happens
  execl(pwent.pw_shell, cp, "-fc", cmd, NULL);

  perror(pwent.pw_shell);
  return(1);
  }

