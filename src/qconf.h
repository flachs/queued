#ifndef __qconf_h__
#define __qconf_h__ 1

typedef struct conf_s
  {
  char *name;
  struct conf_s *next,*head;
  } conf_t;

conf_t *read_conf_file(char *filename);
conf_t *conf_find(conf_t *conf,...);


#endif
