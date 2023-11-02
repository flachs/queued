#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "dlc_option.h"

static char **dlc_envp;
void init_env_stuff(char **envp)
  {
  dlc_envp = envp;
  }

char *dlc_get_env(char *dbk,char *key)
  {
  int len = strlen(key);
  for (char **e=dlc_envp;*e;e++)
    {
    if (! strncmp(*e,key,len-1) && (*e)[len]=='=')
      {
      char *eeq = strchr(*e,'=');
      return(eeq+1);
      }
    }
  return(NULL);
  }

static optdes *default_opt_des ;
static int default_argn ;
static char **default_argp;

void fprintf_option_table(FILE *stream,optdes *opt_des)
  {
  fprintf(stderr,"\noptions:\n");
  
  optdes *table = opt_des ? opt_des : default_opt_des;
  int opt=0;
  int width=0;
  while (1)
    {
    if (! table[opt].opt)
      {
      if (table[opt].des)
        {
        table = (optdes *)table[opt].des;
        opt=0;
        continue;
        }
      else break;
      }

    char *op = table[opt].opt;
    char *eq = strchr(op,'=');
    int len = (eq) ? (eq-op) : strlen(op);
    if (width<len) width = len;
    
    opt++;
    }

  table = opt_des ? opt_des : default_opt_des;
  opt=0;
  while (1)
    {
    if (! table[opt].opt)
      {
      if (table[opt].des)
        {
        table = (optdes *)table[opt].des;
        opt=0;
        continue;
        }
      else break;
      }

    char *op = table[opt].opt;
    char buf[width+1],*p,*q;
    for (q=buf,p=op; *p && *p != '=';*q++=*p++);
    *q=0;

    if (table[opt].val)
      {
      fprintf(stderr,"  %*s: %s (%s)\n",width,buf,
              table[opt].des,table[opt].val);
      }
    else
      {
      fprintf(stderr,"  %*s: %s\n",width,buf,
              table[opt].des);
      }
    
    opt++;
    }
  }

int dlc_parse_args(optdes *opt_des,int argn,char **argp)
  {
  if (!default_opt_des)
    {
    default_opt_des = opt_des;
    default_argn    = argn ;
    default_argp    = argp ;
    }
  
  if (!opt_des) opt_des = default_opt_des;
  if (!argp)
    {
    argn = default_argn;
    argp = default_argp;
    }
    
  char *prog = strrchr(argp[0],'/');
  if (prog) prog++;
  else prog = argp[0];
  int lprog=strlen(prog);
  char PROG[lprog+2],*p,*q;
  for (q=PROG,p=prog; *p ; *q++ = toupper(*p++) ) ;
  *q++ = '_';
  *q++ = 0;

  // look through conf file for option defaults
  
  // look through envs for option defaults
  for (char **e=dlc_envp;*e;e++)
    {
    if (! strncmp(*e,PROG,lprog+1))
      {
      char *eeq = strchr(*e,'=');
      if (! eeq) goto parse_args_next_env;

      char ear[eeq-*e+1];
      for (q=ear,p=(*e)+lprog+1; *p!='=' ; *q++ = tolower(*p++) ) ;
      *q=0;

      for (int opt=0;opt_des[opt].opt;opt++)
        {
        char *op = opt_des[opt].opt;
        char *eq = strchr(op,'=');

        if (eq)
          {
          if (!strncmp(ear,op,eq-op))
            {
            opt_des[opt].val=eeq+1;
            goto parse_args_next_env;
            }
          }
        else
          {
          if (!strcmp(ear,op))
            {
            opt_des[opt].val=eeq+1;
            goto parse_args_next_env;
            }
          }
        }
        
      }
    
  parse_args_next_env:
    ;
    }
  
  // look through args -- override the stuff that is already there
  for (int arg=1;arg<argn;arg++)
    {
    if (argp[arg][0]=='-')
      {
      char *ar = argp[arg]+1;
      for (int opt=0;opt_des[opt].opt;opt++)
        {
        char *op = opt_des[opt].opt;
        char *eq = strchr(op,'=');
        if (eq)
          {
          if (!strncmp(ar,op,eq-op))
            {
            char *aeq = strchr(ar,'=');
            if (aeq)
              {
              opt_des[opt].val=aeq+1;
              goto parse_args_next_arg;
              }
            else
              {
              opt_des[opt].val=argp[arg+1];
              arg++;
              goto parse_args_next_arg;
              }
            }
          }
        else
          {
          if (!strcmp(ar,op))
            {
            opt_des[opt].val=ar;
            goto parse_args_next_arg;
            }
          }
        }
      }

    return arg;
    
  parse_args_next_arg:
    ;
    }
  return argn;
  }

void dlc_append_args(optdes *opt_des)
  {
  // get correct options set
  dlc_parse_args(opt_des,0,NULL);

  optdes *table = default_opt_des;
  int opt=0;
  while (1)
    {
    if (! table[opt].opt)
      {
      if (table[opt].des)
        {
        table = (optdes *)table[opt].des;
        opt=0;
        continue;
        }
      else break;
      }
    opt++;
    }

  // found end of table -- append new table to it
  table[opt].des = (char *)opt_des;
  }


char *dlc_option_value(optdes *opt_des,char *ar)
  {
  optdes *table = opt_des ? opt_des : default_opt_des;
  int arn=strlen(ar);
  
  int opt=0;
  while (1)
    {
    if (! table[opt].opt)
      {
      if (table[opt].des)
        {
        table = (optdes *)table[opt].des;
        opt=0;
        continue;
        }
      else break;
      }

    char *op = table[opt].opt;
    char *eq = strchr(op,'=');
    if (eq)
      {
      if (eq-op == arn && !strncmp(ar,op,eq-op))
        {
        return(table[opt].val);
        }
      }
    else
      {
      if (!strcmp(ar,op))
        {
        return(table[opt].val);
        }
      }    
    
    opt++;
    }

  return(NULL);
  }

void dlc_option_set_default(optdes *opt_des,char *ar,char *val)
  {
  optdes *table = opt_des ? opt_des : default_opt_des;
  int arn=strlen(ar);
  
  int opt=0;
  while (1)
    {
    if (! table[opt].opt)
      {
      if (table[opt].des)
        {
        table = (optdes *)table[opt].des;
        opt=0;
        continue;
        }
      else break;
      }

    char *op = table[opt].opt;
    char *eq = strchr(op,'=');
    if (eq)
      {
      if (eq-op == arn && !strncmp(ar,op,eq-op))
        {
        table[opt].val = val;
        return;
        }
      }
    else
      {
      if (!strcmp(ar,op))
        {
        table[opt].val = val;
        return;
        }
      }    
    
    opt++;
    }
  abort();
  }

char *dlc_get_option(char *dbk,char *key)
  {
  return dlc_option_value(NULL,key);
  }

