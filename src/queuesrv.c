#include "q.h"

#include <sys/types.h>
#include <pwd.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

extern const char *queuedd;
extern const char *slash;

/* this file implements receiving a job from the scheduler
   running it on this host, maintaining info for status
   querries */

/* tracking for the launched jobs */
typedef struct
  {
  pid_t pid;
  time_t starttime;
  joblink_t *jl;
  } pid_info_t;


// need a mutex to gaurd the launched jobs data so that
// multiple threads launched by server can launch jobs
// in parallel
#define max_launched_jobs  (4 K)
static pthread_mutex_t launched_pids_lock;
static volatile int number_launched_jobs = 0, number_current_jobs=0;
static volatile pid_info_t launched_pids[max_launched_jobs];
static volatile time_t free_pids_head = 0;

// called by server to init the mutex before pthreading starts
void launch_init()
  {
  pthread_mutex_init(&launched_pids_lock, NULL);
  }

// called by server as it exits
void launch_cleanup()
  {
  pthread_mutex_destroy(&launched_pids_lock);
  }

// record a forked child in launch_control
void push_launched_pid(pid_t pid,joblink_t *jl)
  {
  time_t now = time(NULL);
  pthread_mutex_lock(&launched_pids_lock);

  time_t it=0;
  if (free_pids_head)
    {
    it = free_pids_head;
    free_pids_head = launched_pids[it].starttime;
    }
  else
    {
    it = number_launched_jobs++;
    }

  launched_pids[it].pid = pid;
  launched_pids[it].jl = jl;
  launched_pids[it].starttime = now;
  number_current_jobs++;

  pthread_mutex_unlock(&launched_pids_lock);
  }

// search job list for pid
static int find_launched_pid(pid_t pid)
  {
  int i;
  for (i=0;i<number_launched_jobs;i++)
    if (launched_pids[i].pid==pid)
      return i;
  return -1;
  }

// search job list for jl
static int find_launched_jl(joblink_t *jl)
  {
  int i;
  for (i=0;i<number_launched_jobs;i++)
    if (launched_pids[i].jl==jl)
      return i;
  return -1;
  }

// search job list for tag
static int find_launched_tag(uint64_t tag)
  {
  int i;
  for (i=0;i<number_launched_jobs;i++)
    if (launched_pids[i].jl->tag==tag)
      return i;
  return -1;
  }

// when job is done remove it from job list
void remove_launched_pid(pid_t pid)
  {
  int it = find_launched_pid(pid);
  
  pthread_mutex_lock(&launched_pids_lock);
  launched_pids[it].pid=0;
  launched_pids[it].starttime=free_pids_head;
  free_pids_head = it;
  --number_current_jobs;
  pthread_mutex_unlock(&launched_pids_lock); 
  }

// foreach loop for jobs -- stop if handler returns != 0
typedef int (felp_func_t)(volatile pid_info_t *,void *);
int foreach_launched_pid(felp_func_t *handler,void *args)
  {
  int i;
  for (i=0;i<number_launched_jobs;i++)
    {
    if (launched_pids[i].pid==0) continue ;
    if (handler(&launched_pids[i],args)) break;
    }
  }

// mark all procs in launched pid trees as inq
int mark_proc_inq_handler(volatile pid_info_t *info,void *va)
  {
  mark_proc_inq(info->pid,info->jl->tag,info->jl->dir,info->jl->u->uid,0);
  return 0;
  }

// help qps.c: mark all procs in all running jobs as being inq
void mark_proc_inqueue()
  {
  pthread_mutex_lock(&launched_pids_lock);
  foreach_launched_pid(&mark_proc_inq_handler,NULL);
  pthread_mutex_unlock(&launched_pids_lock); 
  }

// retrieve the command/wdir/env for a job
// also ensures cmd file is whole, returns NULL if
// cmd file is partially written.
sendhdr_t *get_job_cmd(char *dir,parsed_cmd_t *pc)
  {
  off_t cmdsize=0;
  int dfd = openjob(dir,"cmd",O_RDONLY,&cmdsize);
  
  if (cmdsize==0) return NULL;
  
  char *cbuf = malloc(cmdsize);
  read(dfd,cbuf,cmdsize);
  close(dfd);
  
  sendhdr_t *hdr = (sendhdr_t *)cbuf;

  if (cmdsize != sizeof(*hdr) + hdr->size)
    {
    free(cbuf);
    return NULL;
    }

  if (!pc) return hdr; // only want the header
  
  int argn      = hdr->value[0];
  pc->cmd        = cbuf + sizeof(*hdr);
  pc->wdir       = glue_cmd_spaces(pc->cmd,argn);
  
  int nenviro   = hdr->value[1];
  char *envp    = pc->wdir + strlen(pc->wdir)+1;
  pc->env        = malloc_env_list(envp,nenviro);

  return hdr;
  }

/* when a job finishes, the job dir either gets deleted
   or gets a status file */

const char *rm_rf_command="/bin/rm -rf ";

void update_job_dir_when_done(joblink_t *jl,int status)
  {
  // it is done -- save status
  extern const char *fn_status;  // in q.c
  int sfd = openjob(jl->dir,fn_status,O_WRONLY|O_CREAT,NULL);
  if (sfd>=0)
    {
    dprintf(sfd,"%d\n",status);
    close(sfd);
    }

  // remove job directory if dont want it
  off_t cmdsize;
  int kfd = openjob(jl->dir,"keep",O_RDONLY,&cmdsize);
  if (kfd>=0)
    {
    close(kfd);
    }
  else if (jl->keep>0 && status!=0 || jl->keep>1)
    {
    ;
    }
  else
    {
    char rmcmd[strlen(rm_rf_command)+strlen(jl->dir)+1];
    char *arm=cpystring(rmcmd,rm_rf_command);
    cpystring(arm,jl->dir);
    int tries=0;
    int srv=0;
    while (srv=system(rmcmd))
      {
      fprintf(stderr,"rm failed %d %d\n",srv,tries);
      sleep(1);
      if (tries>4) return;
      tries++;
      }
    }
  }

/* on a thread, fork the job and wait for it to end
   report it done. */
void *launch_control(void *va)
  {
  joblink_t *jl = va;

  parsed_cmd_t pc;
  sendhdr_t *hdr=0;

  // have to wait for job file to be ready in nfs
  while (! hdr)
    {
    hdr = get_job_cmd(jl->dir,&pc);
    }
  
  int child = server_child_fork(hdr->uid,hdr->gid,
                                -1,-1,-1,-1,
                                jl->dir,jl->tag,
                                pc.wdir,pc.cmd,pc.env);

  free(pc.env);
  pc.env = NULL;
  
  push_launched_pid(child,jl);
  
  int status;
  waitpid(child,&status,0);

  remove_launched_pid(child);

  update_job_dir_when_done(jl,status);
  
  // send done message to master
  hdr->kind  = DK_jobdone;
  hdr->size  = 0;
  set_tag_ui32x2_ui64(hdr->value,jl->tag);
  hdr->value[3] = status;
  
  simple_request(getqueuemasterhostname(),
                 "jobdone_status",
                 hdr,NULL);
  free(hdr);
  return 0;
  }

/* server receives jobs from send_job_host and runs
   launch_control */
void server_runjob(server_thread_args_t *client)
  {
  static char *onerror = "error";
  static char *always  = "always";
  
  sendhdr_t hdr = client->hdr;
  int sock = client->sock;

  uidlink_t *ul = find_or_make_uid(hdr.uid);
  joblink_t *jl = make_jl();
  
  jl->u = ul;
  jl->dir = malloc(hdr.size);
  recvn(sock,jl->dir,hdr.size,0);

  jl->tag = get_tag_ui32x2_ui64(hdr.value);

  // have to wait for job file to be ready in nfs
  sendhdr_t *chdr=0;
  while (! chdr)
    chdr = get_job_cmd(jl->dir,NULL);
  free(chdr);

  // now can read parms
  jl->nparms = read_parse_parms(jl->dir,&(jl->parms));
  jl->keep = 0;
  const char *keep = find_parm("keep",jl->nparms,jl->parms,onerror);
  if (!strcmp(keep,onerror))     jl->keep = 1;
  else if (!strcmp(keep,always)) jl->keep = 2;
  
  // no more errors? send reply
  hdr.kind = DK_reply;
  hdr.size = 0;
  send_response(sock,&hdr,NULL);

  // run the job
  launch_control(jl);
  free_jl(jl);
  }

/* find a job in the table and send a TERM to all processes
   in the process group */
int kill_job(uint64_t tag)
  {
  int i = find_launched_tag(tag);

  /* there is a interval in qrun.c/server_child_fork between fork and
     setpgid where the sub process and the server share a process
     group.  killing the sub process during this interval delivers a
     signal also to the server.  so we have to wait for the target
     process to exit the process group of the server before killing
     it.

     this happens when dq multiple jobs in fifo order.  killing a job
     opens a job slot, which is filled by a job that will be soon
     dq. */
  
  pid_t srvpg = getpgid(0);
  pid_t tgtpg = getpgid(launched_pids[i].pid);

  int tries=0;
  while (tgtpg == srvpg)
    {
    fprintf(stderr,"avoid suicide %d %d\n",tgtpg,launched_pids[i].pid,tries);
    sleep(1);
    if (tries>4) return EDEADLK;
    tries++;
    
    tgtpg = getpgid(launched_pids[i].pid);
    }
  
  if (0)
    printf("kj: %d %d %s\n",
         i,launched_pids[i].pid,launched_pids[i].jl->dir);

  int killrv = kill(-launched_pids[i].pid,SIGTERM);
  if (0 && killrv)
    printf("killfailed: (%d) %s\n",errno,strerror(errno));
  
  if (0) printf("KJ\n");
  return killrv ? errno : 0;
  }

// server recieves kill request from send_kill
void server_killjob(server_thread_args_t *client)
  {
  sendhdr_t hdr = client->hdr;
  int sock = client->sock;
  
  uint64_t tag = get_tag_ui32x2_ui64(hdr.value);

  int error=kill_job(tag);

  hdr.kind = DK_reply;
  hdr.size = 0;
  hdr.value[3] = error;
  
  send_response(sock,&hdr,NULL);
  }


