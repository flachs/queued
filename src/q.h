#ifndef __Q_H__
#define __Q_H__ 1

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <byteswap.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <wordexp.h>
#include <stdarg.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "dlc_option.h"
#include "qconf.h"

#define K *1024
#define M K K
#define G M K

#define STRDUPA(s) strcpy(alloca(strlen(s)),s)

// information about the computer hardware.  never changes.
typedef struct
  {
  int cores,      // number of cores
      threads,    // total of hardware threads available>=cores
      bogomips,   // performance metric of a hardware thread
      memory;     // physical RAM installed.
  } computerinfo_t;

// information about a job running under q system
typedef struct
  {
  uint64_t tag;   // jl pointer on master for a job
  uint32_t vsize; // virtual memory size (kB)
  uint32_t rsize; // resident memory size (kB)
  int proc,       // number of processes
      running,    // number of running processes
      threads,    // number of threads
      tty;        // tty number
  uid_t uid;
  char jd[256];
  } running_stats_t;

// information about all jobs running under q system on a host
typedef struct
  {
  int n;
  running_stats_t s[0]; // 0 - external , rest are per running job
  } queuestatus_t;

// information about users logged into the system
typedef struct
  {
  time_t activity;  // time since most recent activity by a user
                    // minimum tty time since last activity
  // long x;  // x_idle cant reliably open a the server
  int users;        // number of active users found by tty activity<30min
                    // a user with multiple tty counts as 1 user.
  } userinfo_t;

// information about how a computer is operating
typedef struct
  {
  time_t uptime,     // seconds computer has been running
         time;       // time on that computer
  int la1,la5,la15;  // load averages 1, 5 and 15 minute
  int memswap,       // size of swap space in Mb
      memused,       // memory used by programs in Mb
      memavail,      // memory still available in Mb
      membufrc;      // memory usec by disk cache/buffers/kernel reclaimable.
  } computerstatus_t;

// roll up of computer status
typedef struct
  {
  int srs;                 // number of runs allocated for
  int nrs;                 // number of running jobs on a computer
  computerinfo_t info;
  userinfo_t user;
  computerstatus_t stat;
  running_stats_t runs[0];
  } statusinfomsg_t;
  
#define max(a,b) \
   ({ typeof (a) _a = (a); \
       typeof (b) _b = (b); \
     _a > _b ? _a : _b; })

#define min(a,b) \
   ({ typeof (a) _a = (a); \
       typeof (b) _b = (b); \
     _a < _b ? _a : _b; })

#define signed_step(x) \
   ({ __typeof__(x) __x = (x); \
     (__x > 0) ? 1 : (__x < 0) ? -1 : 0; })

// message types
typedef enum
  {
  DK_reply,       // reply of success
  DK_trunchdr,    // reply that header is incomplete
  DK_badmagic,    // reply that header had wrong magic info
  DK_reject,      // reply that request has failed
  
  DK_syncecho,
  DK_echo,
  DK_echorep,
  DK_restart,
  DK_terminate,
  
  DK_gettime,     // cl or master to master or server
  DK_timerep,     // reply
  
  DK_getwho,      // cl to master or server
  DK_whorep,
  
  DK_getuptime,   // cl to master or server
  DK_uptime,
  
  DK_getstatus,   // cl to master or master to server
  DK_status,
  
  DK_runcmd,      // cl to server
  DK_rescode,     // server to cl
  
  DK_enqueue,     // cl to master
  DK_dequeue,     // cl to master
  DK_listque,     // cl to master
  DK_listtokens,  // cl to master
  
  DK_runjob,      // master to server
  DK_killjob,     // master to server
  DK_jobdone,     // server to master
  DK_pause  ,     // server to master
  } datakind_t;

// header for messages
typedef struct
  {
  uint64_t magic;
  uint32_t uid,gid;
  datakind_t kind;
  uint32_t size;     // size of payload that follows header
  uint32_t value[6];
  } sendhdr_t;

/* sendhdr_t value     0     1     2     3     4     5
   DK_enqueue        jobgrp
       reply         immed
   DK_run            argn   nenv stdout stderr clientmagic
 */

// server recieves header and goes to handler (forks?)
// this is the data needed to pick it up in the handler
typedef struct
  {
  conf_t *conf;
  int master;
  int sock;
  sendhdr_t hdr;
  struct sockaddr_in addr;
  socklen_t alen;
  } server_thread_args_t;

// main goes to handler for client functions...
typedef int client_func_t(conf_t *conf,int argn,char **argv,char **env);

// for each host handlers
typedef int (feh_func_t)(conf_t *conf,const char *host,void *p);

// copy a 0 terminated string and return a pointer to
// next place to append
static inline char *cpystring(char *b,const char *p)
  {
  while ( *b++=*p++ );
  return b-1;
  }

// copy a portion of a string, return pointer to next place to append
static inline char *cpystringto(char *b,const char *p,const char *e)
  {
  while ( p<e ) *b++=*p++;
  *b = 0;
  return b;
  }

// if a prefix string matches at beginning of buffer return
// pointer to what comes next in buffer
static inline char *strmatch(char *b,char *s)
  {
  char c;
  
  while ( (c=*s) && (c==*b) ) b++,s++;
  if (c==0) return b;
  return 0;
  }

// list for host info
typedef struct hostlist_s
  {
  struct hostlink_s *head,*tail;
  } hostlist_t;

typedef struct hostlink_s
  {
  struct hostlink_s *hn,*hp;      // host next and previous
  struct joblink_s  *head,*tail;  // list of jobs on host
  struct hostlist_s *h;           // pointer head of host list
  time_t sift;                    // when si was last filled
  statusinfomsg_t *si;
  unsigned int used_threads,      // how many threads are allocated
               used_memory,       // how much memory is allocated
               jobs_running;      // how many jobs are running
  char host[];                    // name of host
  } hostlink_t;

// list for user info
typedef struct uidlist_s
  {
  struct uidlink_s *head,*tail;
  } uidlist_t;

typedef struct uidlink_s
  {
  struct uidlink_s *un,*up;      // user next and prev
  struct joblink_s *head,*tail;  // list of jobs for user
  struct uidlist_s *u;           // pointer head of user lest
  char *dir,*name;               // queue dir and user name
  uid_t uid;
  int threads;                   // declared threads running for user
  struct timespec l_start;       // time of last job start for user 
  } uidlink_t;

// list for jobs
typedef struct joblink_s
  {
  struct joblink_s *un,*up;  // uid job list
  struct joblink_s *hn,*hp;  // host job list
  struct uidlink_s *u;       // user link
  struct hostlink_s *h;      // host link
  
  uint64_t tag;              // pointer to jl in server
  gid_t gid;                 // group id 
  struct timespec ct;        // job create time (of dir)
  char *dir;                 // dir of job 
  int jg;                    // job group from user
  int pri;                   // priority from user
  int nparms;                // number of parms
  char **parms;              // array of pointers to parms
  int keep;                  // what do do with dir after done
  int mem,threads;           // how much mem/threads are needed
  } joblink_t;

// information needed to start a job
typedef struct
  {
  char *cmd;    // the command
  char *wdir;   // the working dir
  char **env;   // the env vars
  } parsed_cmd_t ;

// job match spec for listing/dequeuing jobs
typedef enum
  {
  JMS_PRI     = 1<<0,
  JMS_JG      = 1<<1,
  JMS_CMDRE   = 1<<2,
  JMS_CMDSS   = 1<<3,
  JMS_JOBDIR  = 1<<4,
  JMS_TIME    = 1<<5,
  JMS_HOST    = 1<<6,
  JMS_USER    = 1<<7,
  JMS_ALL     = 1<<8
  } jms_spec_t;
  
typedef struct
  {
  jms_spec_t spec; // valid bits
  uid_t uid;       // job owner (-1 for all)
  int   jg;        // job group
  int   pri;       // job priority
  time_t beg,end;  // create time interval
  char value[];    // string data: command, host or dir
  } job_match_spec_t;

// store pointer into 2 consecutive integer array locations  
static inline void set_tag_ui32x2(uint32_t *d,void *tag)
  {
  uint64_t *p = (uint64_t *)d;
  *p = (uint64_t)tag;
  }

// get pointer from 2 consecutive integer array locations  
static inline void *get_tag_ui32x2(uint32_t *d)
  {
  uint64_t *p = (uint64_t *)d;
  return (void *)(*p);
  }

// store 64 bit int into 2 consecutive integer array locations  
static inline void set_tag_ui32x2_ui64(uint32_t *d,uint64_t tag)
  {
  uint64_t *p = (uint64_t *)d;
  *p = tag;
  }

// get 64 bit int from 2 consecutive integer array locations  
static inline uint64_t get_tag_ui32x2_ui64(uint32_t *d)
  {
  uint64_t *p = (uint64_t *)d;
  return *p;
  }

#define clear(it) memset(&it,0,sizeof(it))

// #include "closer.h"
  
// tokens.c
void print_tokens(FILE *stream);
void build_token_table(conf_t *conf);
int  check_tokens(joblink_t *jl);
int  check_tokens_ever(joblink_t *jl);
void claim_tokens(joblink_t *jl);
void release_tokens(joblink_t *jl);

// pidfile.c
void set_pid_dir(char *dirname);
int  bad_magic(uint64_t magic);
int get_magic_for_host(const char *hostname, uint64_t *magic);
void create_pid_file(char *hostname);
uint64_t make_magic(uint64_t *magic);

// uid.c
uidlink_t *uidlist_head();
uidlink_t *find_uid(uid_t uid);
uidlink_t *find_uid_by_name(char *name);
uidlink_t *find_or_make_uid(uid_t uid);

// host.c
void print_host_list(joblink_t *it);
hostlink_t *get_host_state(const char *host);

// joblink.c
joblink_t *make_jl();
void free_jl(joblink_t *jl);

// open_socket.c
size_t recvn(int sock,void *b,size_t len,int flags);
int connected_socket(in_addr_t in_addr,
                     int server_port);
int open_server_socket(int port);
int open_client_socket(const char *serverspec,int server_port,
                       const char *msg);
int get_sock_port(int sockfd);

// q.c
void print_help(optdes *opt_des,char *name,char *msg);
void send_response(int sock,sendhdr_t *hdr,const void *data);
char *cpyhostname(char *b);
char *hostname();
void printlog(char *fmt,...);
int for_each_host(conf_t *conf,const char *hostname,
                  feh_func_t *f,void *p);
int getserviceport();
char *getqueuemasterhostname();
void pthread_detached(void *(*start_routine) (void *), void *arg);

// runimm.c
int run_immediate(conf_t *conf,char *prog,
                  int argn,char **argv,char **env);

// qecho.c
char *simple_request(const char *hostname,const char *message,
                     sendhdr_t *hdr,void *data);
void server_echo(server_thread_args_t *client);
client_func_t echo_client;
void server_status(server_thread_args_t *client);
client_func_t status_client;

// time_format.c
char *format_time(char *buffer,time_t t);
time_t parse_time(char *tos);

// queuesrv.c
queuestatus_t *readqueuestatus();
void update_job_dir_when_done(joblink_t *jl,int status);
void *launch_control(void *va);
int kill_job(uint64_t tag);
void server_killjob(server_thread_args_t *client);
void server_runjob(server_thread_args_t *client);
sendhdr_t *get_job_cmd(char *dir,parsed_cmd_t *pc);
void mark_proc_inqueue();

// qservrst.c
void server_restart(server_thread_args_t *client);
client_func_t restart_client;
void server_terminate(server_thread_args_t *client);
client_func_t terminate_client;

// queue.c
void launch_init();
void launch_cleanup();
const char *find_parm(const char *name,
                      int nparms, char **parml,
                      const char *def);
int read_parse_parms(const char *dir,char ***parmsp);
joblink_t *make_jl();
void server_jobdone(server_thread_args_t *client);
statusinfomsg_t *get_myhost_status(int mypid,statusinfomsg_t **psi);
statusinfomsg_t *get_host_status(const char *host,int client,
                                 statusinfomsg_t **psi);
statusinfomsg_t *get_hl_status(hostlink_t *hl,int client,
                               time_t recent);
int send_kill(const char *host,joblink_t *jl);
void server_enqueue(server_thread_args_t *client);
void server_dequeue(server_thread_args_t *client);
client_func_t enqueue_client;
client_func_t dequeue_client;
client_func_t listqueue_client;
client_func_t listtokens_client;
int openjob(const char *dir,const char *file,int flags,off_t *filesize);
void server_lsq(server_thread_args_t *client);
void server_list(server_thread_args_t *client);
void server_tokens(server_thread_args_t *client);
void load_queue_state(conf_t *conf);

// qrun.c
char *strmatchany(char *b,...);
int calc_arg_size(int argn, char **argv);
char *run_client_calc_env_size(sendhdr_t *hdr,
                               int argn,char **argv,char **env);
void run_client_build_env_string(char *buffer,
                                 int argn,char **argv,char **env,
                                 char *cwd);
char *make_arg_string(int argn,char **argv);
void server_run(server_thread_args_t *client);
void *malloc_env_list(char *envp,int nenviro);
char *glue_cmd_spaces(char *cmd,int argn);
int server_child_fork(uid_t uid,gid_t gid,
                      int csfd,int sifd,int sofd,int sefd,
                      char *jd,uint64_t tag,
                      char *wdir,char *cmd,char **env);
int run_localhost(int needfork,int argn,char **argv,char **env);
int run_remotehost(int multihost,char *rhostname,
                   int argn,char **argv,char **env);

// cpuinfo.c
userinfo_t readuserinfo();
computerinfo_t readcpuinfo();
computerstatus_t readcpustatus();

// qps.c
int mark_proc_inq(int pid,uint64_t tag,char *dir,uid_t uid,int ind);

// tty_stat.c
void tty_stat(time_t *seconds_since_most_recent_activity,
              int *number_of_active_users);

// xidle.c
long x_idle();


#endif
