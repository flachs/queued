#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>

#define MINUTES *60
#define RECENT_ACTIVITY (30 MINUTES)

#define IS_DOT(n) (n[0] == '.' && n[1]==0)
#define IS_DOTDOT(n) (n[0] == '.' && n[1] == '.' && n[2]==0)
#define IS_DOTS(n) (IS_DOT(n) || IS_DOTDOT(n))

#define min(a,b) \
   ({ typeof (a) _a = (a); \
       typeof (b) _b = (b); \
     _a < _b ? _a : _b; })

#define max_value(a) \
   ({ typeof (a) one = 1,zero = 0, \
      ms = (one<<(sizeof(one)*8-1))-1, \
      mu = zero-1; \
     ms>mu ? ms : mu; })

typedef struct
  {
  uid_t uid;
  time_t youngest;
  int count;
  } uidtabe_t;

static uidtabe_t *uidtab=0;
static int uidtabn=0,uidtabl=0;

static uidtabe_t *find_uidtabe(uid_t uid)
  {
  for (int i=0;i<uidtabn;i++)
    if (uid==uidtab[i].uid) return uidtab+i;

  if (uidtabn>=uidtabl)
    {
    uidtabl += 100;
    uidtab = realloc(uidtab,uidtabl*sizeof(uidtab[0]));
    }

  uidtabe_t *rv = &uidtab[uidtabn++];
  rv->uid = uid;
  rv->youngest=max_value(rv->youngest);
  rv->count=0;
  return rv;
  }

  
void tty_stat(time_t *seconds_since_most_recent_activity,
              int *number_of_active_users)
  {
  static char pts[100] = "/dev/pts/";
  const int ptsl = 9;
  struct stat sbuf;

  pts[ptsl]=0;
  DIR *dp = opendir(pts);
  if (!dp)
    {
    return;
    }

  uidtabn=0;
  
  time_t now = time(NULL);
  
  struct dirent *de;
  while (de = readdir(dp))
    {
    if (IS_DOTS(de->d_name)) continue;
    if (! isdigit(de->d_name[0])) continue;

    strcpy(pts+ptsl,de->d_name);
    if (stat(pts,&sbuf)) continue;

    uidtabe_t *te = find_uidtabe(sbuf.st_uid);
    
    time_t age_s = now - sbuf.st_mtim.tv_sec;
    te->youngest = min(te->youngest,age_s);
    te->count++;

    if (0)
      printf("here %d %d %ld %s %ld\n",
             sbuf.st_uid,te->count,te->youngest,
             de->d_name,age_s);
    }
  
  closedir(dp);

  int actives = 0;
  time_t youngest = max_value(youngest);
  for (int i=0;i<uidtabn;i++)
    {
    youngest = min(youngest,uidtab[i].youngest);
    actives += uidtab[i].youngest < RECENT_ACTIVITY;
    if (0)
      printf("there %d %ld %ld %d\n",
             uidtab[i].uid,uidtab[i].youngest,
             youngest,actives);
    }
  
  if (seconds_since_most_recent_activity)
    *seconds_since_most_recent_activity = youngest;
  if (number_of_active_users)
    *number_of_active_users = actives;
  }

