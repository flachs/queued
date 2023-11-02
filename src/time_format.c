#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

char *format_time(char *buffer,time_t t)
  {
  time_t tl = t;
  struct tm tm;
  localtime_r(&tl,&tm);
  
  sprintf(buffer,"%04d/%02d/%02d@%02d:%02d:%02d",
          1900+tm.tm_year,tm.tm_mon+1,tm.tm_mday,
          tm.tm_hour,tm.tm_min,tm.tm_sec);
  
  return buffer;
  }

time_t parse_time(char *tos)
  {
  time_t now = time(0);
  
  struct tm tm;
  localtime_r(&now,&tm);
  
  char *bos = tos[0]=='-' ? tos+1 : tos;
  char *eos = strchr(bos,0);
  char *at=strchr(bos,'@');
  char *fcol=strchr(bos,':'),*scol=fcol?strchr(fcol+1,':'):0;
  char *fdiv=strchr(bos,'/'),*sdiv=fdiv?strchr(fdiv+1,'/'):0;
  char *bdate=(at && fdiv>at) ? at+1 : fdiv ? bos : 0;
  char *edate=(at>bdate) ? at : eos;
  char *btime=(at && fcol>at) ? at+1 : fcol ? bos : at ? at+1 : bos;
  char *etime=(at>btime) ? at : eos;

  time_t delta=0;
  if (bdate)
    {
    if (sdiv)
      { // [YY]YY/MM/DD
      tm.tm_year = atoi(bdate) + ((fdiv-bdate)>2 ? -1900 : 2000-1900);
      tm.tm_mon  = atoi(fdiv+1)-1;
      tm.tm_mday = atoi(sdiv+1);
      }
    else if (fdiv)
      { // MM/DD
      tm.tm_mon  = atoi(bdate)-1;
      tm.tm_mday = atoi(fdiv+1);
      }
    else
      { // DD
      tm.tm_mday = atoi(bdate);
      delta += 24*60*60*tm.tm_mday;
      }
    }
  if (btime)
    {
    if (scol)
      { // HH:MM:SS
      tm.tm_hour = atoi(btime);
      tm.tm_min = atoi(fcol+1);
      tm.tm_sec = atoi(scol+1);
      delta += tm.tm_hour*60*60 + tm.tm_min*60 + tm.tm_sec;
      }
    else if (fcol)
      { // HH:MM
      tm.tm_hour = atoi(btime);
      tm.tm_min = atoi(fcol+1);
      tm.tm_sec = 0;
      delta += tm.tm_hour*60*60 + tm.tm_min*60;
      }
    else
      { // HH
      tm.tm_hour = atoi(btime);
      tm.tm_min = 0;
      tm.tm_sec = 0;
      delta += tm.tm_hour*60*60;
      }
    
    if (tm.tm_hour<12 && ( ((etime[-1]|' ')=='p') ||
                           ((etime[-2]|' ')=='p') ))
      tm.tm_hour += 12;
    }

  if (bos>tos) return now-delta;
  return mktime(&tm);
  }

