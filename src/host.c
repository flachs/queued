#include "q.h"
#include "list.h"

hostlist_t hostlist;

void print_host_list(joblink_t *it)
  {
  hostlink_t *hl = hostlist.head;
  int hc = 0;

  for (;hl && hc<10;hl=hl->hn,hc++) 
    {
    printf("host %d %s %p %p (%p)\n",hc,hl->host,hl->head,hl->tail,it);
    joblink_t *jl = hl->head;
    int c=0;
    for (; jl && c<10 ; jl=jl->hn,c++)
      {
      printf("   c %d h %p p %p it %p n %p t %p: %s\n",
             c,
             jl->h->head,jl->hp,jl,jl->hn,jl->h->tail,
             jl->dir);
      }
    }
  }

hostlink_t *get_host_state(const char *host)
  {
  hostlink_t *p = hostlist.head;
  for (; p ; p = p->hn)
    if (! strcmp(p->host,host)) return p;

  p = calloc(sizeof(hostlink_t)+strlen(host)+1,1);
  strcpy(p->host,host);

  add_link_to_head(&hostlist,p,h);

  return p;
  }

