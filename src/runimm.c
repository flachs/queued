#include "q.h"

extern optdes options[];

int run_client(int multihost,char *rhostname,
                int argn,char **argv,char **env)
  {
  // need to talk to server if
  //    * different host or 
  //    * want stdout prefixing or
  //    * send stdin to all procs
  int needserv = dlc_option_value(NULL,"n") != 0 ||
                 dlc_option_value(NULL,"sin") != 0;
  
  if (needserv || strcmp(rhostname,hostname()))
    return run_remotehost(multihost,rhostname,argn,argv,env);

  // want to run locally -- need to fork if there will
  // be subsequent hosts
  return run_localhost(multihost,argn,argv,env);
  }

int run_immediate(conf_t *conf,char *prog,
                  int argn,char **argv,char **env)
  {
  char *rhostname="all";
  char *p=0;
  
  if (argn>1 && (p=strmatch(argv[0],"host=")))
    {
    rhostname = p;
    argv++;
    argn--;
    }
  
  if (argn<1)
    print_help(options,prog,"[host=hostname|groupname] cmd args");

  conf_t *group = conf_find(conf,"group",rhostname,NULL);
  if (group)
    {
    int rv=0;
    int hc=0;
    for (; group ; group = group->next)
      {
      int multihost = hc>0 || group->next;
      int rac = run_client(multihost,group->name,argn,argv,env) != 0;
      hc++;
      rv |= rac;
      }
    return rv;
    }

  // search group all for the named host
  group = conf_find(conf,"group","all",NULL);
  for (; group ; group = group->next)
    {
    if (!strcmp(group->name,rhostname)) break;
    }

  if (group)
    return run_client(0,rhostname,argn,argv,env);

  fprintf(stderr,"%s: is not a group nor a host in group all\n",rhostname);
  return 1;
  }


