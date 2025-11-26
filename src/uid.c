#include "q.h"
#include "list.h"

extern const char *queuedd;

uidlist_t uidlist;

uidlink_t *uidlist_head()
  {
  return uidlist.head;
  }

uidlink_t *find_uid(uid_t uid)
  {
  uidlink_t *p = uidlist.head;
  for (; p ; p = p->un)
    if (p->uid == uid) return p;
  return 0;
  }

uidlink_t *find_uid_by_name(char *name)
  {
  uidlink_t *p = uidlist.head;
  for (; p ; p = p->un)
    if (! strcmp(p->name,name)) return p;
  return 0;
  }

static inline int getpwuid_r_bufsize()
  {
  int bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (bufsize<0) return 16 K;
  return bufsize;
  }

uidlink_t *build_uid(struct passwd *pwe)
  {
  uidlink_t *p = calloc(sizeof(uidlink_t),1);

  add_link_to_head(&uidlist,p,u);
  
  p->dir = malloc(strlen(pwe->pw_dir)+1+
                  strlen(queuedd)+1);
  p->name = malloc(strlen(pwe->pw_name)+1);
  strcpy(p->name,pwe->pw_name);
  
  char *qd = cpystring(p->dir,pwe->pw_dir);
  *qd++ = '/';
  qd = cpystring(qd,queuedd);
  p->uid = pwe->pw_uid;
  return p;
  }

uidlink_t *find_or_make_uid(uid_t uid)
  {
  uidlink_t *p = find_uid(uid);
  if (p) return p;

  struct passwd pwent,*pwresult;
  char buf[getpwuid_r_bufsize()];
  getpwuid_r(uid,&pwent,buf,sizeof(buf),&pwresult);

  return build_uid(&pwent);
  }

uidlink_t *find_or_make_pwe(struct passwd *pwe)
  {
  uidlink_t *p = find_uid(pwe->pw_uid);
  if (p) return p;

  return build_uid(pwe);
  }

