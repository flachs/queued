#ifndef __qconf_h__
#define __qconf_h__ 1

#include <sys/stat.h>

typedef struct confl_s
  {
  char *name;
  struct confl_s *next,*head;
  } confl_t;

typedef struct conf_s
  {
  char *name;
  char *file;
  off_t size;
  time_t mtim;
  confl_t *head;
  } conf_t;

conf_t *read_conf_file(char *filename);
conf_t *reload_conf_file(conf_t *conf);
confl_t *conf_find(conf_t *conf,...);


#endif
