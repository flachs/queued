#ifndef DLC_OPTION_H
#define DLC_OPTION_H 1
#include <stdio.h>

typedef struct optdes_s
  {
  char *opt;
  char *des;
  char *val;
  } optdes;

void fprintf_option_table(FILE *stream,optdes *opt_des);
void init_env_stuff(char **envp);
void init_conf_file(char *filename);

char *dlc_get_env(char *dbk, char *key);
int dlc_parse_args(optdes *opt_des,int argn,char **argp);
void dlc_append_args(optdes *opt_des);
char *dlc_option_value(optdes *opt_des,char *ar);
void dlc_option_set_default(optdes *opt_des,char *ar,char *val);

#endif

