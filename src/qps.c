#include "q.h"

#include "proc.h"

/* build and analyze the processes found in /proc
   some of which belong to running jobs, others
   are unrelated */

// a process entry
// needs to be greater than TASK_COMM_LEN, 
// TS_COMM_LEN is 32 in taskstats.h so would pick that,
// but round up so that entry is 128 bytes.
#define MAX_COMLEN (72)
typedef struct proctabe_s
  {
  int next,chhead;
  unsigned int uid,pid,ppid,tty,threads;
  unsigned char state;
  joblink_t *tag;
  uint64_t vsize,rsize;
  char *top;
  char com[MAX_COMLEN];
  } proctabe_t;

// a table of process entries
typedef struct
  {
  int n,tl;
  proctabe_t *t;
  } proctab_t;

static proctab_t proctab;

// find a pid in the process table return the index
int find_pid_index(proctabe_t *t,int n,int pid)
  {
  int i=0;
  for (;i<n;i++)
    {
    if (t[i].pid==pid) return i;
    }
  return -1;
  }

// build_proctab requests read_proc_table from proc.c
// sends this routine as reciever for the entries found
int recieve_proc_entry(void *arg,pid_t pid,proc_pid_stat_t *procp)
  {
  proctab_t *pt = &proctab;
  proctabe_t *p = 0;
  
  int pi = find_pid_index(pt->t,pt->n,pid);
  if (pi<0)
    { // didnt find it, make new entry
    if (pt->n==pt->tl)
      {
      int ntl = 2*pt->tl;
      pt->t = realloc(pt->t,ntl*sizeof(pt->t[0]));
      memset(pt->t+pt->n,0,(ntl-pt->n)*sizeof(pt->t[0]));
      pt->tl = ntl;
      }
    p = &pt->t[pt->n++];
    p->pid = pid;
    p->tty = procp->tty;
    strncpy(p->com,procp->cmd,MAX_COMLEN-1);
    p->com[MAX_COMLEN]=0;
    }
  else
    {
    p = &pt->t[pi];
    }
  
  p->ppid = procp->ppid;
  p->state = procp->state;
  p->vsize = procp->vsize>>20; // Mbytes
  p->rsize = procp->rss>>8;    // Mbytes
  p->threads = procp->nlwp;
  p->tty = procp->tty;
  p->top = 0;
  p->tag = 0;
  p->next = -1;
  p->chhead = -1;
  return 0;
  }

/* for every entry find entry of parent pid, add the child to
   the parent's child list */
void build_proc_tree()
  {
  int nde = proctab.n;
  proctabe_t *pt = proctab.t;

  // erase previous linked lists
  for (int i=0;i<nde;i++) pt[i].chhead = pt[i].next = -1;

  for (int i=1;i<nde;i++)
    {
    int pi = find_pid_index(proctab.t,proctab.n,pt[i].ppid);
    if (pi<0) continue;
    pt[i].next = pt[pi].chhead;
    pt[pi].chhead = i;
    }
  }

/* clear the state of all of the entries in the proc table,
   so that we can find out which procs disappear by finding
   procs with 0 state after the more recent data is imported */
void clear_proc_table_state(proctabe_t *pte,int n)
  {
  proctabe_t *end=pte+n;
  
  for (; pte<end; pte++) pte->state=0;
  }

/* find a entry with a particular state, or 
   if state==256 a non-zero state */
proctabe_t *find_next_proc_state(proctabe_t *pt,
                                 proctabe_t *end,
                                 unsigned int state)
  {
  for (;pt<end;pt++)
    {
    unsigned int pts = pt->state;
    if (state==256 && pts>0 || state==pts) return pt;
    }
  return 0;
  }

/* squeeze out 0 state procs out of the state table */
void clean_proc_table( )
  {
  proctabe_t *pt  = proctab.t+1,
             *end = proctab.t+proctab.n,
             *fe;

  while (pt = find_next_proc_state(pt,end,0))
    {
    if (fe = find_next_proc_state(pt,end,256))
      {
      memcpy(pt,fe,sizeof(*pt));
      fe->state=0;
      }
    else
      {
      proctab.n = pt - proctab.t;
      return;
      }
    }
  }

void print_proc_table(const char *logfilename)
  {
  FILE *fp = fopen(logfilename,"w");
  if (! fp) abort();

  for (int pi=0;pi<proctab.n;pi++)
    {
    fprintf(fp,"%d: pid=%d next=%d chhead=%d tag=%lx top=%s com=%s\n",
            pi,proctab.t[pi].pid,proctab.t[pi].next,proctab.t[pi].chhead,
            proctab.t[pi].tag,proctab.t[pi].top,proctab.t[pi].com);
    }
  
  fclose(fp);
  }

/* mark a tree of parents->children as belonging to a job
   running in the q -- might be more consistent to base
   belong to the q on process group */
int mark_proc_inq(int pid,joblink_t *tag,char *dir,uid_t uid,int ind)
  {
  proctabe_t *pt=proctab.t;

  // only top is indexed by the pid, after that by
  // table index
  int pi = (ind==0) ? find_pid_index(pt,proctab.n,pid) : pid;
  if (pi<0)
    { // process is gone, but not yet removed from launched_pids
    return 0;
    }

  pt[pi].tag=tag;
  pt[pi].top=(ind==0) ? dir : 0;
  pt[pi].uid=uid;
  
  if (0)
    {
    for (int i=0;i<ind;i++) putchar(' ');
    printf("mpq %p %d: "
           "pid=%d next=%d chhead=%d tag=%lx top=%s com=%s\n",
           pt[pi].top,
           pi,pt[pi].pid,pt[pi].next,pt[pi].chhead,
           pt[pi].tag,pt[pi].top,pt[pi].com);
    }
  
  int it = pt[pi].chhead;
  for ( ; it>=0; it = pt[it].next)
    mark_proc_inq(it,tag,dir,uid,ind+2);
  return 0;
  }

/* read and catagorize processes */
void build_proctab()
  {
  if (proctab.n==0)
    {
    proctab.n = 1;
    proctab.tl = 1000;
    size_t tabsize = proctab.tl * sizeof(proctabe_t);
    proctab.t = malloc(tabsize);
    memset(proctab.t,0,tabsize);
    
    proctab.t[0].state='X';
    proctab.t[0].next = proctab.t[0].chhead = -1;
    strcpy(proctab.t[0].com,"root");
    }

  // mark state=dead but leave root entry alone
  clear_proc_table_state(proctab.t+1,proctab.n-1);

  // read /proc - update old entries & make new entries
  read_proc_table( recieve_proc_entry, NULL );
  clean_proc_table( ); // remove dead entries
  build_proc_tree( );  // ppid has branches to pid

  // mark all procs in launched pid trees as inq
  mark_proc_inqueue();
  }

void print_children(int lev,int pid,proctab_t *pt)
  {
  if (! pt)
    {
    pt=&proctab;
    }
  
  int pi = find_pid_index(pt->t,pt->n,pid);
  if (pi>=0)
    {
    for (int i=0;i<lev;i++) { putchar(' ');putchar(' '); }
    printf("%d (%s)\n",pid,pt->t[pi].com);
    }
  
  if (proctab.t) free(proctab.t);
  }

// find a tag in the running_stats table
running_stats_t *find_tag_slot(running_stats_t *s,int n,joblink_t *tag)
  {
  running_stats_t *p = s+1, *e = s+n;
  
  for (;p<e;p++)
    if (p->tag == tag) return p;
  return s;
  }

/* return lots of queue status info
   -- used by get_myhost_status */
queuestatus_t *readqueuestatus(int mypid)
  {
  static int nrs = 0;
  static queuestatus_t *info;
  static time_t last;

  build_proctab();

  proctabe_t *pt=proctab.t,
            *end=pt+proctab.n;

  int nslots = 1;  // need one for external procs
  for (pt=proctab.t ; pt<end; pt++)
    if (pt->top) nslots++;

  if (nslots>nrs)
    {
    int nnrs = nslots+16;
    size_t sz = ( sizeof(queuestatus_t)+
                  nnrs*sizeof(running_stats_t));
    info = realloc(info, sz);
    nrs = nnrs;
    }

  info->n = nslots;
  memset(info->s,0,nrs*sizeof(running_stats_t));
  
  nslots = 1; // slot zero is for external
  for (pt=proctab.t ; pt<end; pt++)
    {
    if (pt->top)
      {
      info->s[nslots].tag = pt->tag;
      strcpy(info->s[nslots].jd,pt->top);
      info->s[nslots].uid = pt->uid;
      nslots++;
      }
    }
  
  for (pt=proctab.t ; pt<end; pt++)
    {
    if (pt->pid == mypid) continue;

    running_stats_t *s = find_tag_slot(info->s,nslots,pt->tag);

    int ttymaj = pt->tty;
    int ttymin = (ttymaj & 0xff) | (((ttymaj>>20) & 0xfff)<<8);
    ttymaj = (ttymaj>>8) && 0xff;

    //if (s==info->s && pt->tty==0) continue;
    
    if (0 && nslots>1)
      printf("%d: %d %c %d %lld %d %-20s\n",
              s-info->s,pt->tty,
              pt->state,pt->pid,
              pt->vsize,pt->threads,
              pt->com);

    s->proc    ++;
    s->running += pt->state == 'R';
    s->threads += pt->threads;
    s->vsize   += pt->vsize;
    s->rsize   += pt->rsize;
    }
  
  return info;
  }

