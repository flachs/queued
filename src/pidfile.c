
#define _GNU_SOURCE

#include "q.h"

#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static int fd_piddir=-1;
static char pidfilename[4096];

static void remove_pid_file()
  {
  unlinkat(fd_piddir,pidfilename,0);
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
  if (fd_piddir>=0)
    {
    close(fd_piddir);
    fd_piddir = -1; 
    }
  
  char *eos = cpystring(piddir,dn);
  len_piddir = eos-piddir;

  if (eos[-1] != '/') return;
  eos[-1]=0;
  len_piddir --;
  }

typedef struct magic_cache_entry_t
  {
  struct magic_cache_entry_t *n;
  uint64_t magic;
  time_t   tvsec;
  char host[];
  } magic_cache_entry_t;

magic_cache_entry_t *magic_cache_h;
  
int get_magic_for_host(const char *hostname,uint64_t *magic)
  {
  if (fd_piddir < 0)
    fd_piddir = open(piddir,O_RDONLY|O_DIRECTORY|O_PATH);

  if (fd_piddir < 0)
    {
    printlog("Error: can not find pid dir\n");
    return 1;
    }

  magic_cache_entry_t *p;
  struct stat sb;
  
  char fn[4096];
  char *ext = cpystring(fn,hostname);
  char *eos = cpystring(ext,".pid");

  // find hostname in cache
  for (p=magic_cache_h; p ; p = p->n)
    if (! strcmp(p->host, hostname)) break;

  int src = fstatat(fd_piddir,fn,&sb,0);
  if (p && ! src &&
      sb.st_mtim.tv_sec == p->tvsec)
    { // cache entry exists & is current
    *magic = p->magic;
    return 0;
    }

  int count = 0;
  int fd;

  // open or wait for nfs
  while ( (fd = openat(fd_piddir,fn,O_RDONLY)) < 0 && count++<3)
    while (sleep(3)) ;
  
  if (fd<0)
    {
    printlog("cant find magic for %s\n",hostname);
    return 1;
    }

  fstat(fd,&sb);
  
  sendhdr_t hdr;
  read(fd,&hdr,sizeof(hdr));
  close(fd);

  if (!p)
    {
    p = malloc(sizeof(*p)+ext-fn+1);
    p->n = magic_cache_h;
    magic_cache_h = p;
    strcpy(p->host,hostname);
    }

  p->tvsec = sb.st_mtim.tv_sec;
  p->magic = *magic = hdr.magic;
  return 0;
  }

void create_pid_file(char *fn)
  {
  sendhdr_t hdr;
  
  memset(&hdr,0,sizeof(hdr));
  
  mymagic = getrand64(& hdr.magic);
  
  hdr.value[0] = getpid();

  if (fd_piddir < 0)
    fd_piddir = open(piddir,O_RDONLY|O_DIRECTORY|O_PATH);

  if (fd_piddir < 0)
    {
    mkdir(piddir,0775);
    fd_piddir = open(piddir,O_RDONLY|O_DIRECTORY|O_PATH);
    }

  if (fd_piddir < 0)
    {
    printlog("Error: can not create pid dir - Exiting\n");
    exit(1);
    }

  char *p=cpystring(pidfilename,fn);
  p = cpystring(p,".pid");
  
  int pf = openat(fd_piddir, pidfilename,
                  O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR);
  if (pf < 0)
    {
    printlog("Error: can not create pid file - Exiting\n");
    exit(1);
    }
  
  atexit(remove_pid_file);
  write(pf,&hdr,sizeof(hdr));
  close(pf);
  return;
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
