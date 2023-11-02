#include "proc.h"

#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>


#define KLF "l"
#define STRTOUKL strtoul

#define likely(x)       __builtin_expect(!!(x),1)
#define unlikely(x)     __builtin_expect(!!(x),0)
#define expected(x,y)   __builtin_expect((x),(y))

int read_proc_stat(int pid, proc_pid_stat_t *restrict P)
  {
  char filename[PROCPATHLEN] ;
  sprintf(filename,"/proc/%d/stat",pid);
  int fd = open(filename,O_RDONLY);
  if (fd<0) return 0;

  // > 50int64 * 5chars/int16 * 4int16/int64 +
  //   50sep spaces + 64cmd chars + 2 parens + 1 terminator
  char buf[2048];  
  int bytes=read(fd,buf,sizeof(buf));
  close(fd);

  buf[bytes]=0;  // null terminate
  
  /* fill in default values for older kernels */
  memset(P,0,sizeof(*P));
  
  P->processor = 0;
  P->rtprio = -1;
  P->sched = -1;
  P->nlwp = 0;

  int c;
  char *bp = buf,*EOB=buf+bytes;
  while ((c = *bp++) != '(') if (bp>EOB) return 0;
  char *cmd = P->cmd;
  while ((c = *bp++) != ')')
    {
    if (bp>EOB) return 0;
    else *cmd++ = c;
    }
  
  *cmd=0;

  bp++; // skip " "
  if (bp>EOB) return 0;

  int fields = 
  sscanf(bp,
         "%c "
         "%d %d %d %d %d "
         "%lu "
         "%lu %lu %lu %lu "
         "%llu %llu %llu %llu "  /* utime stime cutime cstime */
         "%ld %ld "
         "%d "
         "%ld "
         "%llu "  /* start_time */
         "%lu "   // vsize
         "%ld "   // rss
         "%lu "   // rss_rlim
         "%"KLF"u %"KLF"u "  // start_code, end_code
         "%"KLF"u %"KLF"u %"KLF"u "
         /* discard, no RT signals & Linux 2.1 used hex */
         "%*s %*s %*s %*s " 
         "%"KLF"u "  //wchan
         "%*u %*u "  /* nswap and cnswap dead for 2.4.xx and up */
         "%d %d "    // exit_signal, processor
         "%lu %lu "  // rtprio, sched
         "%llu " // delayacct_blkio_ticks 
         "%lu "  // guest_time            
         "%ld "  // cguest_time           
         "%lu "  // start_data            
         "%lu "  // end_data              
         "%lu "  // start_brk             
         "%lu "  // arg_start             
         "%lu "  // arg_end               
         "%lu "  // env_start             
         "%lu "  // env_end               
         "%d "   // exit_code             
         ,
         &P->state,
         &P->ppid, &P->pgrp, &P->session, &P->tty, &P->tpgid,
         &P->flags,
         &P->min_flt, &P->cmin_flt, &P->maj_flt, &P->cmaj_flt,
         &P->utime, &P->stime, &P->cutime, &P->cstime,
         &P->priority, &P->nice,
         &P->nlwp,
         &P->alarm,
         &P->start_time,
         &P->vsize,
         &P->rss,
         &P->rss_rlim,
         &P->start_code, &P->end_code,
         &P->start_stack, &P->kstk_esp, &P->kstk_eip,
         /* P->signal, P->blocked, P->sigignore, P->sigcatch, */
         &P->wchan,
         /* &P->nswap, &P->cnswap, */
         &P->exit_signal, &P->processor,  /* 2.2.1 ends with "exit_signal" */
         &P->rtprio, &P->sched,  /* both added to 2.5.18 */
         &P->delayacct_blkio_ticks,
         &P->guest_time           ,
         &P->cguest_time          ,
         &P->start_data           ,
         &P->end_data             ,
         &P->start_brk            ,
         &P->arg_start            ,
         &P->arg_end              ,
         &P->env_start            ,
         &P->env_end              ,
         &P->exit_code);

  if(!P->nlwp) P->nlwp = 1;
  
  return fields;
  }

int filetype(int d_type,char *name)
  {
  if (d_type == DT_UNKNOWN)
    {
    struct stat sbuf;
    memset(&sbuf,0,sizeof(sbuf));
    stat(name,&sbuf);
    if (S_ISDIR(sbuf.st_mode)) return(DT_DIR);
    return(DT_REG);
    }
  return d_type;
  }

#define IS_DOT(n) (n[0] == '.' && n[1]==0)
#define IS_DOTDOT(n) (n[0] == '.' && n[1] == '.' && n[2]==0)
#define IS_DOTS(n) (IS_DOT(n) || IS_DOTDOT(n))

void read_proc_table(recieve_proc_entry_handler_t *handler,
                    void *handlerarg)
  {
  char procid[100] ;
  strcpy(procid,"/proc/");
  
  DIR *dp = opendir(procid);

  proc_pid_stat_t proc;
  
  struct dirent *de;
  while (de = readdir(dp))
    {
    if (IS_DOTS(de->d_name)) continue;
    if (! isdigit(de->d_name[0])) continue;
    int pid = atoi(de->d_name);
    
    strcpy(procid+6,de->d_name);
    int ftype = filetype(de->d_type,procid);
    if (ftype != DT_DIR) continue;
    
    if (read_proc_stat(pid,&proc))
      {
      if (handler(handlerarg,pid,&proc)) break;
      }
    }
  
  closedir(dp);
  }
