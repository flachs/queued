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

#ifndef conf_debug
#define conf_debug (0)
#endif

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

void pushconf(confl_t **conf,int dims,int nl,...)
  {
  va_list ap;
  
  va_start(ap,nl);
  confl_t **k = conf;
  int i=0;
  char *s = va_arg(ap,char *);
  confl_t *c,*n;
  
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
  if (conf_debug) fprintf(stderr,"caladd %p %s\n",n,s);
  
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
  if (conf_debug) fprintf(stderr,"calnxe %p %s\n",n,list[ne]);
  n->next = *k;
  n->name = list[ne];
  ne++;
  c = *k = n;
  
  if (ne<nl) goto nexte;
  
  va_end(ap);
  
  }

char *readfile(char *filename,char *buf,off_t size)
  {
  int fd = open(filename,O_RDONLY);
  if (fd<0) return NULL;

  // suck in file
  if (!buf) buf = malloc(size);
  
  int n = read(fd,buf,size);
  close(fd);
  buf[n]=0;

  return buf;
  }

conf_t *read_conf_file(char *filename)
  {
  struct stat statbuf;
  if (stat(filename,&statbuf)) return NULL;

  // round up
  int granule = 1<<12;
  off_t size = (statbuf.st_size + 1 + granule) & ~( granule-1 );

  char *text = delete_comments(readfile(filename,NULL,size));
  
  if (!text) return NULL;
  
  conf_t *rv = malloc(sizeof(conf_t));
  rv->name = filename;
  rv->size = statbuf.st_size;
  rv->mtim = statbuf.st_mtim.tv_sec;
  
  rv->file = text;
  rv->head = 0;
  
  char *p = text;
  char *type,*lhs,*list;
  while (p = conf_parse_stmt(p,&type,&lhs,&list))
    {
    int n = count_items(list);
    char *elements[n];
    find_items(elements,list);
    pushconf(&(rv->head),2,n,type,lhs,elements);
    }
  
  return rv;
  }

void free_conf_index(confl_t *head)
  {
  if (!head) return ;
  if (conf_debug) fprintf(stderr,"fci %p %s\n",head,head->name);

  free_conf_index(head->head);
  free_conf_index(head->next);
  
  if (conf_debug) fprintf(stderr,"free %p %s\n",head,head->name);
  free(head);
  }


conf_t *reload_conf_file(conf_t *conf)
  {
  struct stat statbuf;
  
  if (conf_debug) fprintf(stderr,"rcf: %s\n",conf->name);
  if (stat(conf->name,&statbuf)) return conf;

  if (conf->mtim >= statbuf.st_mtim.tv_sec) return conf;
  
  free_conf_index(conf->head);
  conf->head = 0;
  
  // round up
  int granule = 1<<12;
  off_t size = (statbuf.st_size + 1 + granule) & ~( granule-1 );

  if (size > conf->size)
    {
    conf->file = realloc(conf->file,size);
    conf->size = size;
    }
  
  char *text = delete_comments(readfile(conf->name,conf->file,size));
  if (!text) return NULL;

  char *p = text;
  char *type,*lhs,*list;
  while (p = conf_parse_stmt(p,&type,&lhs,&list))
    {
    int n = count_items(list);
    char *elements[n];
    find_items(elements,list);
    
    pushconf(&conf->head,2,n,type,lhs,elements);
    }
  
  return conf;
  }

confl_t *conf_find(conf_t *conf,...)
  {
  va_list ap;
  
  va_start(ap,conf);

  confl_t *c = conf->head;
  
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
