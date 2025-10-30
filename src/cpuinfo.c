#include "q.h"

char *strbackspc(char *p)
  {
  while (isspace(*p)) p--;
  return p+1;
  }

int fnmatch(char *a,char *b,char *e)
  {
  int c;
  while ((c=*a) && b<e && c==*b) a++,b++;
  return b>=e;
  }

userinfo_t readuserinfo()
  {
  static userinfo_t info;

  tty_stat(&info.activity,&info.users);
  
  return info;
  }

computerinfo_t readcpuinfo()
  {
  static computerinfo_t info;
  int cores[8];
  int threads[8];
  int bogomips[8];
  int phya=0;
  int phym=0;
  int phy=-1;

  if (info.cores) return info;
  
  char buf[16 K];
  FILE *fp = fopen("/proc/cpuinfo","r");
  if (! fp) exit(1);
  
  while (1)
    {
    if (! fgets(buf,sizeof(buf)-1,fp)) break;
    
    char *colon = strchr(buf,':');
    if (!colon) continue;
    
    char *efn = strbackspc(colon-1);
    if (fnmatch("physical id",buf,efn))
      { // this comes first
      phy = atoi(colon+1);
      phym = 1<<phy;
      if ( phym & phya ) phym = 0;
      else phya |= phym;
      }
    else if (phym && fnmatch("cpu cores",buf,efn))
      {
      cores[phy] = atoi(colon+1);
      }
    else if (phym && fnmatch("siblings",buf,efn))
      {
      threads[phy] = atoi(colon+1);
      }
    else if (phym && fnmatch("bogomips",buf,efn))
      {
      bogomips[phy] = atoi(colon+1);
      }
    }
  
  fclose(fp);

  for (phy=0;phy<8;phy++)
    {
    if (phya & (1<<phy)) continue;
    info.threads  += threads[phy];
    info.cores    += cores[phy];
    info.bogomips += cores[phy] * bogomips[phy];
    }
  info.bogomips /= info.cores;
  
  fp = fopen("/proc/meminfo","r");
  if (! fp) exit(1);

  while (1)
    {
    if (! fgets(buf,sizeof(buf)-1,fp)) break;
    if (isspace(buf[0])) break;
    
    char *colon = strchr(buf,':');
    char *efn = strbackspc(colon-1);
    if (fnmatch("MemTotal",buf,efn)) // in KB
      info.memory = atoi(colon+1)/(1 K); // in MB
    }
  
  fclose(fp);  
  return info;
  }

computerstatus_t readcpustatus()
  {
  static computerstatus_t info;
  static time_t last;
  
  time_t now = info.time = time(NULL);
  if (now - last<30)
    { // provide old info if called rapidly
    info.time = now;
    info.uptime += now-last;
    return info;
    }

  last = now;
  
  char buf[16 K];
  FILE *fp = fopen("/proc/uptime","r");
  if (! fp) exit(1);

  double secs;
  fscanf(fp,"%lg",&secs);
  info.uptime = secs;

  fclose(fp);

  fp = fopen("/proc/loadavg","r");
  if (! fp) exit(1);

  float la1=0,la2=0,la3=0;
  fscanf(fp,"%f%f%f",&la1,&la2,&la3);
  
  info.la1  = la1*100;
  info.la5  = la2*100;
  info.la15 = la3*100;

  fclose(fp);
  
  fp = fopen("/proc/meminfo","r");
  if (! fp) exit(1);

  int mem = 0;
  int swap = 0;
  int freem = 0;
  int cachd = 0;
  int bufrs = 0;
  int avail = 0;
  while (1)
    {
    if (! fgets(buf,sizeof(buf)-1,fp)) break;
    if (isspace(buf[0])) break;
    
    char *colon = strchr(buf,':');
    char *efn = strbackspc(colon-1);
    if (fnmatch("MemTotal",buf,efn))       mem   += atoi(colon+1)/(1 K);
    if (fnmatch("SwapTotal",buf,efn))      swap  += atoi(colon+1)/(1 K);
    if (fnmatch("MemFree",buf,efn))        freem += atoi(colon+1)/(1 K);
    if (fnmatch("SwapFree",buf,efn))       freem += atoi(colon+1)/(1 K);
    if (fnmatch("Cached",buf,efn))         cachd += atoi(colon+1)/(1 K);
    if (fnmatch("SReclaimable",buf,efn))   cachd += atoi(colon+1)/(1 K);
    if (fnmatch("Buffers",buf,efn))        bufrs += atoi(colon+1)/(1 K);
    if (fnmatch("MemAvailable",buf,efn))   avail += atoi(colon+1)/(1 K);
    }

  int total = mem + swap;
  info.memswap = swap;
  info.memused = total - freem - bufrs - cachd;
  info.memavail = avail;
  
  fclose(fp);  

  return info;
  }

