#include "q.h"
#include "dlc_string.h"

typedef struct token_s
  {
  struct token_s *n;
  int   pool,taken,marker;
  char  name[0];
  } token_t;

token_t *tokens = 0;

token_t *find_token(char *name)
  {
  token_t *t;
  for (t=tokens;t;t=t->n)
    { 
    if (!strcmp(name,t->name)) return t;
    }
  return 0;
  }

void mark_tokens(int v)
  {
  token_t *t;
  for (t=tokens;t;t=t->n) t->marker = v;
  }

void rm_tokens(int v)
  {
  token_t *t,**p=&tokens;
  while ( *p )
    {
    if ((*p)->marker == v)
      {
      t = *p;
      *p = (*p)->n;
      free(t);
      }
    else
      {
      p = &((*p)->n);
      }
    }
  }

void print_tokens(FILE *stream)
  {
  token_t *t;
  for (t=tokens;t;t=t->n)
    fprintf(stream,"%s %d %d\n",t->name,t->pool,t->taken);
  }

void dlc_tokens(dlc_string **stream)
  {
  token_t *t;
  for (t=tokens;t;t=t->n)
    dlc_string_caf(stream,"%s %d %d\n",t->name,t->pool,t->taken);
  }

void build_token_table(conf_t *conf)
  {
  confl_t *token = conf_find(conf,"token",NULL);

  mark_tokens(1);
  for ( ; token;token = token->next)
    {
    //printf("token %s %s\n",token->name,token->head->name);
    token_t *t = find_token(token->name);
    if (!t) 
      {
      t = malloc(sizeof(token_t)+strlen(token->name)+1);
      t->n = tokens;
      tokens = t;
      t->taken = 0;
      strcpy(t->name,token->name);
      }
    t->pool = atoi(token->head->name);
    t->marker = 0;
    }
  rm_tokens(1);
  }

int check_tokens(joblink_t *jl)
  {
  int nparms = jl->nparms;
  char **parml = jl->parms;
  
  for (int i=0;i<nparms;i++)
    {
    token_t *t=find_token(parml[2*i]);
    if (t)
      {
      int req = atoi(parml[2*i+1]);
      if (req > (t->pool - t->taken)) return 0;
      }
    }
  return 1;
  }

void claim_tokens(joblink_t *jl)
  {
  int nparms = jl->nparms;
  char **parml = jl->parms;
  
  for (int i=0;i<nparms;i++)
    {
    token_t *t=find_token(parml[2*i]);
    if (t)  t->taken += atoi(parml[2*i+1]);
    }
  }

void release_tokens(joblink_t *jl)
  {
  int nparms = jl->nparms;
  char **parml = jl->parms;
  
  for (int i=0;i<nparms;i++)
    {
    token_t *t=find_token(parml[2*i]);
    if (t)  t->taken -= atoi(parml[2*i+1]);
    }
  }

