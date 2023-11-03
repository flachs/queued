#include "qconf.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

static char *delete_comments(char *p)
  {
  if (!p) return p;
  
  char c,*rv=p;
  int state=0;

  while (c=*p)
    {
    if (c=='#') state=1;
    if (c=='\n') state=0;
    if (state) *p++ = ' ';
    else p++;
    }
  return rv;
  }

char *conf_parse_stmt(char *p,
                      char **type,char **lhs,char **list)
  {
  while (isspace(*p)) p++;

  if (! isalpha(*p)) return NULL;

  *type = p;

  while (isalnum(*p)) p++;
  while (isspace(*p)) *p++=0;

  if (! isalpha(*p)) return NULL;
  
  *lhs = p;

  while (isalnum(*p)) p++;
  while (isspace(*p)) *p++=0;

  if ( *p != '=' ) return NULL;
  p++;
  
  while (isspace(*p)) *p++=0;

  
  if (! isalnum(*p) && *p!='/') return NULL;

  *list = p;

  while (*p && *p!=';') p++;
  
  if (*p) // ; 
    {
    *p=0;
    return p+1;
    }
  return NULL;
  }

int count_items(char *p)
  {
  int rv = 0;

  while(1)
    {
    while (isspace(*p)) p++;
    if (*p == 0) return rv;
    rv ++;
    while (isgraph(*p)) p++;
    if (*p == 0) return rv;
    }
  }

void find_items(char *l[],char *p)
  {
  int rv = 0;

  while(1)
    {
    while (isspace(*p)) p++;
    if (*p == 0) return;

    l[rv] = p;
    rv ++;
    
    while (isgraph(*p)) p++;
    if (*p == 0) return;
    *p++=0;
    if (*p == 0) return;
    }
  }

void pushconf(conf_t **conf,int dims,int nl,...)
  {
  va_list ap;
  
  va_start(ap,nl);
  conf_t **k = conf;
  int i=0;
  char *s = va_arg(ap,char *);
  conf_t *c,*n;
  
nextlev:
  c = *k;
  while (c && strcmp(c->name,s)) c=c->next;
  if (c)
    { // found it
    k = &c->head;
    i++;
    s = va_arg(ap,char *);
    if (i<dims) goto nextlev;
    goto list;
    }
  
  // not there -- add it
addit:
  n = calloc(sizeof(conf_t),1);
  n->next = *k;
  n->name = s;
  *k=n;
  k = &n->head;
  i++;
  s = va_arg(ap,char *);
  if (i<dims) goto addit;

list:; // s is a pointer an array of pointers

  char **list = (char **)s;
  int ne = 0;
  c = *k;

nexte:
  while (c && strcmp(c->name,list[ne])) c=c->next;
  if (c)
    { // found it
    ne++;
    if (ne<nl) goto nexte;
    }
  // not there add it
  n = calloc(sizeof(conf_t),1);
  n->next = *k;
  n->name = list[ne];
  ne++;
  c = *k = n;
  
  if (ne<nl) goto nexte;
  
  va_end(ap);
  
  }

char *readfile(char *filename)
  {
  int fd = open(filename,O_RDONLY);
  if (fd<0) return NULL;

  // suck in file
  struct stat statbuf;
  fstat(fd,&statbuf);
  off_t size = statbuf.st_size;
  char *buf = malloc(size+1);
  read(fd,buf,size);
  close(fd);
  buf[size]=0;

  return buf;
  }

conf_t *read_conf_file(char *filename)
  {
  char *text = delete_comments(readfile(filename));
  if (!text) return NULL;
  
  char *p = text;
  char *type,*lhs,*list;
  conf_t *conf=0;
  while (p = conf_parse_stmt(p,&type,&lhs,&list))
    {
    int n = count_items(list);
    char *elements[n];
    find_items(elements,list);
    
    pushconf(&conf,2,n,type,lhs,elements);
    }
  
  return conf;
  }

conf_t *conf_find(conf_t *c,...)
  {
  va_list ap;
  
  va_start(ap,c);
  
  const char *s = va_arg(ap,char *);
  
  while (c && s)
    {
    while (c && strcmp(s,c->name))
      c = c->next;
    
    if (!c)
      {
      va_end(ap);
      return NULL;
      }
    
    const char *n = va_arg(ap,char *);
    if (c->head==0 && n==0)
      {
      va_end(ap);
      return c;
      }
    
    s = n;
    c = c->head;
    }

  va_end(ap);
  return c;
  }
