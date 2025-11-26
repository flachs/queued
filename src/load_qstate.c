
#define _GNU_SOURCE

#include "q.h"

#include <sys/wait.h>

#include "list.h"

extern const char *queuedd;
extern const char *fn_status; 
extern const char *fn_machine;
extern const char *fn_cmd; 

//extern const char *queuedd;

typedef struct prilist_s
  {
  struct joblink_s *head,*tail;
  } prilist_t;

prilist_t run_list,sub_list;

joblink_t *find_job_tag(joblink_t *tag)
  {
  for (joblink_t *jl=run_list.head; jl ; jl=jl->xn)
    if (jl->tag == tag)
      {
      remove_link(jl,x);
      return jl;
      }
  return (joblink_t *)tag;
  }

joblink_t *find_job_dir(uidlink_t *ul,char *dir)
  {
  for (joblink_t *jl=sub_list.head; jl ; jl=jl->xn)
    if (! strcmp(jl->dir,dir))
      {
      remove_link(jl,x);
      return jl;
      }
  
  return 0;
  }

joblink_t *read_jobinfo_at(int fd)
  {
  off_t sz_cmd;
  int fd_cmd = openjobat(fd,fn_cmd,O_RDONLY,&sz_cmd,NULL);

  if (fd_cmd < 0) return 0;
  
  joblink_t *jl = make_jl();
  
  struct stat qdir_stat;
  fstat(fd,&qdir_stat);    

  sendhdr_t cmd;
  read(fd_cmd,&cmd,sizeof(cmd));
  close(fd_cmd);
  
  jl->gid = cmd.gid;
  jl->jg  = cmd.value[0];
  jl->pri = cmd.value[1];
  jl->nparms  = read_parse_parmsat(fd,&(jl->parms));
  jl->ct      = qdir_stat.st_mtim;
  
  return jl;
  }


void load_queue_state(conf_t *conf)
  {
  setpwent();

  struct passwd *pwe;

  int bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (bufsize<0) bufsize = 16 K;
  char buf[bufsize];
  
  while (pwe = getpwent())
    {
    char *hdn = cpystring(buf,pwe->pw_dir);
    *hdn++ = '/';
    char *qdn = cpystring(hdn,queuedd);
    
    DIR *qd = opendir(buf);
    if (! qd) continue;

    // this user has used q
    printf("%s %d %s > %s\n",pwe->pw_name,pwe->pw_uid,pwe->pw_dir,buf);
    uidlink_t *ul = find_or_make_pwe(pwe);

    struct dirent *je;
    hostlink_t *hl;
    joblink_t  *jl;
    
    while (je = readdir(qd))
      {
      if (je->d_name[0] == '.') continue;
      printf("  %s\n",je->d_name);

      *qdn = '/';
      cpystring(qdn+1,je->d_name);
      int fd_qdir = open(buf,O_RDONLY|O_DIRECTORY|O_PATH);
      if (fd_qdir<0) continue;

      printf("  open %s\n",buf);
      struct timespec  mtime;
      int fd_status,fd_machine;
      off_t sz_machine;
      
      if ((fd_status = openat(fd_qdir,fn_status,O_RDONLY)) >=0)
        { // job is done
        close(fd_status);
        }
      else if ((fd_machine = openjobat(fd_qdir,fn_machine,O_RDONLY,
                                       &sz_machine,&mtime)) >=0)
        { // job was started - have to account for it
        ul->l_start = later(mtime,ul->l_start);
        
        char machine[sz_machine];
        read(fd_machine,machine,sz_machine);
        close(fd_machine);

        char c,*pidp = machine,*thrp=NULL,*tagp=NULL;
        while ((c=pidp[0]) && (c!=' ')) pidp++;
        *pidp++ = 0;
        unsigned   jpid =              strtoul(pidp,&thrp,10);
        unsigned   jthr =              strtoul(thrp,&tagp,10);
        joblink_t *jtag = (joblink_t *)strtoul(tagp,NULL,16);
        
        printf("mach: %s %d %016lp\n",machine,
               jpid,jthr,jtag);

        hl = get_host_state(machine);
        jl = read_jobinfo_at(fd_qdir);
        
        jl->tag     = jtag;
        jl->dir     = strdup(hdn);
        jl->threads = jthr;

        insert_into_list_sorted(ul,jl);     // put job in user list
        add_link_to_head(&(run_list),jl,x); // already running job list
        account_job_start(hl,jl);           // mach list, manage mem / tokens
        }
      else if (jl = read_jobinfo_at(fd_qdir))
        {
        // might get a message 
        add_link_to_head(&(sub_list),jl,x); // already submitted list
        insert_into_list_sorted(ul,jl);     // put job in user list
        }
      
      // could also check for xxx file
      close(fd_qdir);
      }
    
    closedir(qd);
    }
  }

