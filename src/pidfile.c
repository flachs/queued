#include "q.h"

#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>

static char pn[4096];

static void remove_pid_file()
  {
  unlink(pn);
  }

uint64_t getrand64(uint64_t *r)
  {
  FILE *dr;

  if (! (dr=fopen("/dev/random","r")))
    {
    fprintf(stderr,"cant open /dev/random\n");
    return 1;
    }

  uint64_t internalr;
  if (!r) r = &internalr;
  
  *r=0;
  while (*r==0) fread(r,sizeof(*r),1,dr);

  fclose(dr);
  return *r;
  }

static uint64_t mymagic;

int  bad_magic(uint64_t magic)
  {
  if (magic==mymagic) return 0;
  
  fprintf(stderr,"magic %016llx  my magic %016llx\n",magic,mymagic);
  
  return 1;
  }

static int  len_piddir;
static char piddir[4096];
void set_pid_dir(char *dn)
  {
  char *eos = cpystring(piddir,dn);
  len_piddir = eos-piddir;
  if (eos[-1] == '/') return;
  *eos='/';
  len_piddir ++;
  }

typedef struct magic_cache_entry_t
  {
  struct magic_cache_entry_t *n;
  uint64_t magic;
  char host[];
  } magic_cache_entry_t;

magic_cache_entry_t *magic_cache_h;
  
uint64_t get_magic_for_host(const char *hostname)
  {
  magic_cache_entry_t *p;
  
  for (p=magic_cache_h; p ; p = p->n)
    if (! strcmp(p->host, hostname)) return p->magic;

  char *fn  = piddir+len_piddir;
  char *ext = cpystring(fn,hostname);
  char *eos = cpystring(ext,".pid");

  FILE *fp = fopen(piddir,"r");
  if (fp)
    {
    sendhdr_t hdr;
    fread(&hdr,1,sizeof(hdr),fp);
    fclose(fp);

    p = malloc(sizeof(*p)+ext-fn+1);
    p->n = magic_cache_h;
    magic_cache_h = p;
    p->magic = hdr.magic;
    strcpy(p->host,hostname);
    return p->magic;
    }
  
  printf("cant find magic for %s\n",hostname);
  return 0;
  }

void create_pid_file(char *fn)
  {
  sendhdr_t hdr;
  
  memset(&hdr,0,sizeof(hdr));
  
  mymagic = getrand64(& hdr.magic);
  
  hdr.value[0] = getpid();
  
  DIR *vtq = opendir(piddir);
  if (vtq) closedir(vtq);
  else mkdir(piddir,0775);

  char *p=cpystring(pn,piddir);
  p = cpystring(p,fn);
  p = cpystring(p,".pid");

  atexit(remove_pid_file);
  
  int pf = open(pn,O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  if (pf>=0)
    {
    write(pf,&hdr,sizeof(hdr));
    close(pf);
    }
  }

uint64_t make_magic(uint64_t *magic)
  {
  if (! mymagic)
    mymagic = getrand64(magic);
  
    {
    if (magic) *magic = mymagic;
    return mymagic;
    }

  return mymagic;
  }
