#include <stdio.h>
#include "../dl/src/dlc_string.h"
#include "trio.h"

int main(int argn, char **argv,char **env)
  {
  dlc_string *ds;
  
  dlc_string_caf(&ds,"this %b\n",5);
  fputs(ds->t,stdout);
  
  return 0;
  }

  
