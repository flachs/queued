#define _GNU_SOURCE

#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <ctype.h>
#include <stddef.h>

#include "dlc_string.h"
#include "trio/trio.h"

dlc_list *dlc_list_new(dlc_list **tp,size_t elemsize)
  {
  if (! *tp)
    {
    dlc_list *s = malloc(sizeof(dlc_list));
    *tp = s;
    TAG_DLCLIST(s);
    s->s = 0;
    s->l = 0;
    s->e = elemsize;
    s->t = 0;
    }
  }

void *dlc_list_rz(dlc_list **tp,dlc_list_rz_op op,size_t numelems)
  {
  dlc_list *s = *tp;
  
  assert( *tp );

  switch (op)
    {
    case DLC_LIST_RZ_INC: numelems = numelems + s->l; break;
    case DLC_LIST_RZ_DEC:
      if (s->l < numelems) abort();
      
      numelems = s->l - numelems;
      break;
    }
  
  if (! s->t)
    {
    s->s = 2*numelems;
    s->t = calloc(s->s,s->e);
    }
  else if (s->s < numelems)
    {
    int newsize = 2*numelems;
    int newbytes = newsize*s->e;
    s->t = realloc(s->t,newbytes);
    int oldbytes = s->s*s->e;
    char *news = s->t+oldbytes;
    
    memset(news,0,newbytes-oldbytes);
    s->s = 2*numelems;
    }
  void *rv = s->t + s->l*s->e;
  s->l = numelems;
  return (void *)rv;
  }

void dlc_list_free(dlc_list **tp)
  {
  if (! tp || ! *tp || !IS_DLCSTRING(*tp)) abort();
  
  if (*tp)
    {
    if ((*tp)->t) free((*tp)->t);
    free(*tp);
    *tp = 0;
    }
  }

void dlc_list_cpy(dlc_list **tp,dlc_list *sp)
  {
  if (!sp || sp->l==0)
    {
    if (*tp) (*tp)->l = 0;
    return;
    }
    
  if (*tp && (*tp)->e != sp->e)
    {
    fprintf(stderr,"dlc_list_cpy: type mismatch %lld %lld\n",
            (*tp)->e,sp->e);
    abort();
    }

  dlc_list_new(tp,sp->e);
  dlc_list_rz(tp,DLC_LIST_RZ_ABS,sp->l);
  memcpy((*tp)->t,sp->t,sp->e * sp->l);
  }

void *dlc_list_ne_splice(dlc_list **tp,size_t elemsize,
                         size_t pos,size_t rmn,size_t adn)
  {
  dlc_list_new(tp,elemsize);
  size_t es = (*tp)->e;
  
  if (adn>rmn)
    { // adding elements
    size_t net = adn-rmn;
    dlc_list_rz(tp,DLC_LIST_RZ_INC,net);
    if (pos+net < (*tp)->l)
      memmove((*tp)->t + (pos+net) * es,
              (*tp)->t + (pos) * es,
              ((*tp)->l-pos-net) * es);

    if (pos+adn < (*tp)->l)
      memset((*tp)->t + (pos) * es, 0, adn * es);
    
    return((*tp)->t + (pos) * es);
    }
  
  if (adn<rmn)
    { // removing elements
    size_t net = rmn-adn;
    if (pos+net < (*tp)->l)
      memmove((*tp)->t + (pos) * es,
              (*tp)->t + (pos+net) * es,
              ((*tp)->l-pos-net) * es);
    dlc_list_rz(tp,DLC_LIST_RZ_DEC,rmn-adn);
    
    memset((*tp)->t + (pos) * es, 0, adn * es);
    return((*tp)->t + (pos) * es);
    }

  // just re-using elements
  memset((*tp)->t + (pos) * es, 0, adn * es);
  return (*tp)->t + pos * es;
  }

void *dlc_list_ne_append(dlc_list **tp,size_t elemsize,size_t net)
  {
  dlc_list_new(tp,elemsize);
  size_t es = (*tp)->e;
  size_t pos = (*tp)->l;
  dlc_list_rz(tp,DLC_LIST_RZ_INC,net);
  return((*tp)->t + (pos) * es);
  }

void dlc_list_e_delete(dlc_list **tp,size_t pos,size_t net)
  {
  if (net==0) return;
  
  if (! *tp) abort();
  if (pos >= (*tp)->l) abort();
  if (pos+net > (*tp)->l) abort();
  
  size_t es = (*tp)->e;

  if ((*tp)->l > pos+net)
    memmove((*tp)->t + pos*es,
            (*tp)->t + (pos+net)*es,
            ((*tp)->l-pos-net)*es);
  dlc_list_rz(tp,DLC_LIST_RZ_DEC,net);
  }

dlc_string *dlc_string_new(dlc_string **tp)
  {
  if (! *tp)
    {
    dlc_string *s = malloc(sizeof(dlc_string));
    *tp = s;
    TAG_DLCSTRING(s);
    s->s = 0;
    s->l = 0;
    s->e = 1;
    s->t = 0;
    }
  return(*tp);
  }

dlc_string *dlc_string_rz(dlc_string **tp,size_t needlength)
  {
  dlc_string *s = dlc_string_new(tp);
  
  needlength ++;  // room for a null
  if (s->t && ! s->s)
    { // fake
    abort();
    }
    
  if (! s->t)
    {
    s->s = needlength*2;
    s->t = malloc(s->s);
    }
  else if (s->s < needlength)
    {
    s->s = needlength*2;
    s->t = realloc(s->t,s->s);
    }
  return(s);
  }

void dlc_string_free(dlc_string **tp)
  {
  if (*tp)
    {
    if ((*tp)->t) free((*tp)->t);
    free(*tp);
    *tp = 0;
    }
  }

size_t dlc_string_lee(void *s,char *e)
  {
  dlc_nodeb *n = (dlc_nodeb *)s;
  if (IS_DLCPARSENODE(s)) return(n->eot - n->t);
  
  char *sc = (char *)s;
  int is_dlcstring = IS_DLCSTRLIKE(sc);
  dlc_string *ss = (dlc_string *)s;

  if (is_dlcstring) return(ss->l);

  if (e) return(e-sc);
  
  return( strlen(sc) );
  }

size_t dlc_string_len(void *s)
  {
  if (!s) return 0;
  
  dlc_nodeb *n = (dlc_nodeb *)s;
  if (IS_DLCPARSENODE(s)) return(n->eot - n->t);
  
  char *sc = (char *)s;
  int is_dlcstring = IS_DLCSTRLIKE(sc);
  dlc_string *ss = (dlc_string *)s;

  if (is_dlcstring) return(ss->l);
  return( strlen(sc) );
  }

int dlc_string_has_funny_chars(void *s)
  {
  char *p;
  
  if (IS_DLCPARSENODE(s))
    {
    dlc_nodeb *n = (dlc_nodeb *)s;
    for (p=n->t ; p<n->eot ; p++)
      {
      int c=*p;
      if ( ! ( ( c>='a' && c<='z') ||
               ( c>='A' && c<='Z') ||
               ( c>='0' && c<='9') ||
               ( c>='.' ) ) ) return 1;
      }
    return 0;
    }
  
  if (IS_DLCSTRLIKE(s))
    {
    dlc_string *ss = (dlc_string *)s;
    p = ss->t;
    char *eot = p+ss->l;
    for ( ; p<eot ; p++)
      {
      int c=*p;
      if ( ! ( ( c>='a' && c<='z') ||
               ( c>='A' && c<='Z') ||
               ( c>='0' && c<='9') ||
               ( c>='.' ) ) ) return 1;
      }
    return 0;
    }
  
  p = (char *)s;
  for ( ; *p ; p++)
    {
    int c=*p;
    if ( ! ( ( c>='a' && c<='z') ||
             ( c>='A' && c<='Z') ||
             ( c>='0' && c<='9') ||
             ( c>='.' ) ) ) return 1;
    }
  return 0;
  }


void dlc_strcpe(char *tp,char *s,char *e)
  {
  char *sc = (char *)s;
  int is_dlcstring = IS_DLCSTRLIKE(sc);
  dlc_string *ss = (dlc_string *)s;
  
  char *p=tp;
  if (is_dlcstring)
    {
    s=ss->t;
    e=s + ss->l;
    }
  
  while (s<e) *p++ = *s++;
  *p=0;
  }

void dlc_strcpy(char *tp,void *s)
  {
  dlc_nodeb *n = (dlc_nodeb *)s;
  if (IS_DLCPARSENODE(s))
    {
    dlc_strcpe(tp,n->t,n->eot);
    return;
    }
  
  char *sc = (char *)s;
  int is_dlcstring = IS_DLCSTRLIKE(sc);
  dlc_string *ss = (dlc_string *)s;
  
  strcpy(tp,is_dlcstring ? ss->t : sc);
  }

void dlc_string_cpe(dlc_string **tp,char *s,char *e)
  {
  char *sc = (char *)s;
  int is_dlcstring = IS_DLCSTRLIKE(sc);
  dlc_string *ss = (dlc_string *)s;
  
  size_t needlength = is_dlcstring ? ss->l : e ? e-s : strlen(s);
  dlc_string *ts = dlc_string_rz(tp,needlength);

  char *p=ts->t;
  if (is_dlcstring) s=ss->t;
  
  while (s<e) *p++ = *s++;
  *p=0;
  
  ts->l = needlength;
  }

void dlc_string_cpy(dlc_string **tp,const void *s)
  {
  dlc_nodeb *n = (dlc_nodeb *)s;
  if (IS_DLCPARSENODE(s))
    {
    dlc_string_cpe(tp,n->t,n->eot);
    return;
    }
  
  char *sc = (char *)s;
  int is_dlcstring = IS_DLCSTRLIKE(sc);
  dlc_string *ss = (dlc_string *)s;
  
  size_t needlength = is_dlcstring ? ss->l : strlen(sc);
  dlc_string *ts = dlc_string_rz(tp,needlength);

  strcpy(ts->t,is_dlcstring ? ss->t : sc);
  ts->l = needlength;
  }

void dlc_string_cep(dlc_string **tp,char *s,char *e)
  {
  int needlength = (*tp) ? (*tp)->l : 0;
  needlength += e-s;

  dlc_string *ts = dlc_string_rz(tp,needlength);

  char *p=ts->t+ts->l;
  while (s<e) *p++ = *s++;
  *p=0;
  
  ts->l = needlength;
  }

void dlc_string_ac(dlc_string **tp,int c)
  {
  int needlength = (*tp) ? (*tp)->l : 0;
  needlength ++;

  dlc_string *ts = dlc_string_rz(tp,needlength);

  ts->t[ts->l++] = c;
  ts->t[ts->l] = 0;
  }

void dlc_string_cat(dlc_string **tp,const void *s)
  {
  dlc_nodeb *n = (dlc_nodeb *)s;
  
  if (!s) return; // empty source, do nothing....
  
  if (IS_DLCPARSENODE(s))
    {
    dlc_string_cep(tp,n->t,n->eot);
    return;
    }
  
  char *sc = (char *)s;
  int is_dlcstring = IS_DLCSTRLIKE(sc);
  dlc_string *ss = (dlc_string *)s;

  int needlength = (*tp) ? (*tp)->l : 0;
  needlength += is_dlcstring ? ss->l : strlen(sc);

  dlc_string *ts = dlc_string_rz(tp,needlength);

  strcpy(ts->t+ts->l,is_dlcstring ? ss->t : sc);
  ts->l = needlength;
  }

void dlc_strcep(char *tp,char *s,char *e)
  {
  char *p=tp;
  while (*p) p++;
  while (s<e) *p++ = *s++;
  *p=0;
  }

void dlc_strcat(char *tp,void *s)
  {
  if (!s) return; // empty source, do nothing....
  
  if (IS_DLCPARSENODE(s))
    {
    dlc_nodeb *n = (dlc_nodeb *)s;
    dlc_strcep(tp,n->t,n->eot);
    return;
    }
  
  char *sc = (char *)s;
  int is_dlcstring = IS_DLCSTRLIKE(sc);
  dlc_string *ss = (dlc_string *)s;

  strcat(tp,is_dlcstring ? ss->t : sc);
  }

void dlc_string_ine(dlc_string **tp,int point,char *s,char *e)
  {
  int origlength = (*tp) ? (*tp)->l : 0;
  
  if (point<0 || point>=origlength)
    {
    dlc_string_cep(tp,s,e);
    return;
    }
  
  int addlength  = e - s;
  int needlength = origlength + addlength;

  dlc_string *ts = dlc_string_rz(tp,needlength);
  memmove(ts->t+point+addlength,ts->t+point,origlength-point);

  char *p=ts->t+point;
  while (s<e) *p++ = *s++;
  ts->t[needlength]=0;
  ts->l = needlength;
  }

void dlc_string_ins(dlc_string **tp,int point,void *s)
  {
  dlc_nodeb *n = (dlc_nodeb *)s;

  if (!s) return;
  
  if (IS_DLCPARSENODE(s))
    {
    dlc_string_ine(tp,point,n->t,n->eot);
    return;
    }

  int origlength = (*tp) ? (*tp)->l : 0;
  
  if (point<0 || point>=origlength)
    {
    dlc_string_cat(tp,s);
    return;
    }
  
  char *sc = (char *)s;
  int is_dlcstring = IS_DLCSTRLIKE(sc);
  dlc_string *ss = (dlc_string *)s;

  int addlength  = is_dlcstring ? ss->l : strlen(sc);
  int needlength = origlength + addlength;

  // make room for everything
  dlc_string *ts = dlc_string_rz(tp,needlength);
  memmove(ts->t+point+addlength,ts->t+point,origlength-point);
  memmove(ts->t+point, is_dlcstring ? ss->t : sc , addlength);
  ts->t[needlength]=0;
  ts->l = needlength;
  }
  
void dlc_string_rpr(dlc_string **tp,int point,int len,char *s,char *e)
  {
  int origlength = (*tp) ? (*tp)->l : 0;
  
  if (point<0 || point>=origlength)
    {
    fprintf(stderr,"rpr point out of range %d\n",point);
    dlc_string_cep(tp,s,e);
    return;
    }

  int newlength  = e - s;
  int addlength  = newlength - len;
  int needlength = origlength + addlength;

  dlc_string *ts = dlc_string_rz(tp,needlength);

  if (0)
    {
    fprintf(stderr,"rpr: %d %d %d %d\n",
            origlength,newlength,addlength,needlength);

    fputr(ts->t+point-10,ts->t+point,stderr);
    fputs("<",stderr);
    fputr(ts->t+point,ts->t+point+len,stderr);
    fputs("|",stderr);
    fputr(s,e,stderr);
    fputs(">",stderr);
    fputr(ts->t+point+len,ts->t+point+len+10,stderr);
    fputs("\n",stderr);
    }
  
  memmove(ts->t+point+newlength,ts->t+point+len,origlength-point-len);
  memmove(ts->t+point,s,newlength);
  
  ts->t[needlength]=0;
  ts->l = needlength;
  }

void dlc_string_rep(dlc_string **tp,int point,int len,void *s)
  {
  dlc_nodeb *n = (dlc_nodeb *)s;
  
  if (IS_DLCPARSENODE(s))
    {
    dlc_string_rpr(tp,point,len,n->t,n->eot);
    return;
    }

  int origlength = (*tp) ? (*tp)->l : 0;
  
  if (point<0 || point>=origlength)
    {
    fprintf(stderr,"rep point out of range %d\n",point);
    dlc_string_cat(tp,s);
    return;
    }
  
  char *sc = (char *)s;
  int is_dlcstring = IS_DLCSTRLIKE(sc);
  dlc_string *ss = (dlc_string *)s;

  if (is_dlcstring)
    {
    dlc_string_rpr(tp,point,len,ss->t,ss->l+ss->t);
    return;
    }

  int newlength  = strlen(sc);
  dlc_string_rpr(tp,point,len,sc,sc+newlength);
  }


int dlc_rstrcmp(void *ds,char *de,void *ms,char *me)
  {
  char *dc = (char *)ds;
  int ds_is_parsenode = IS_DLCPARSENODE(dc);
  int ds_is_dlcstring = IS_DLCSTRLIKE(dc);
  dlc_string *dss = (dlc_string *)ds;
  dlc_nodeb *dsn = (dlc_nodeb *)ds;

  if (ds_is_parsenode)
    {
    dc = dsn->t;
    de = dsn->eot;
    }
  else if (ds_is_dlcstring)
    {
    dc = dss->t;
    de = dc + dss->l;
    }
  else if (! de)
    de = dc + strlen(dc);

  char *mc = (char *)ms;
  int ms_is_parsenode = IS_DLCPARSENODE(mc);
  int ms_is_dlcstring = IS_DLCSTRLIKE(mc);
  dlc_string *mss = (dlc_string *)ms;
  dlc_nodeb *msn = (dlc_nodeb *)ms;

  if (ms_is_parsenode)
    {
    mc = msn->t;
    me = msn->eot;
    }
  else if (ms_is_dlcstring)
    {
    mc = mss->t;
    me = mc + mss->l;
    }
  else if (! me)
    me = mc + strlen(mc);

  if (0)
    {
    fprintf(stderr,"rstrcmp ");
    fputr(dc,de,stderr);
    fprintf(stderr," <> ");
    fputr(mc,me,stderr);
    fprintf(stderr," : ");
    }
  
  for ( ; dc<de && mc<me ; dc++, mc++)
    {
    if (0) fprintf(stderr,"%c%c ",*dc,*mc);
    if (*dc < *mc) return(-1);
    if (*dc > *mc) return(1);
    }
  
  if (0) fprintf(stderr,"%d <> %d ",me-mc , de-dc);
  if (me-mc > de-dc) return(-1);
  if (me-mc < de-dc) return(1);
  return(0);
  }

char *dlc_string_chr(void *ds,char *de,int c)
  {
  char *dc = (char *)ds;
  int ds_is_dlcstring = IS_DLCSTRLIKE(dc);
  dlc_string *dss = (dlc_string *)ds;

  if (ds_is_dlcstring)
    {
    dc = dss->t;
    de = dc + dss->l;
    }
  else if (! de)
    de = dc + strlen(ds);

  for ( ; dc<de ; dc++)
    {
    if ( *dc == c ) return(dc);
    }
  return(NULL);
  }

void fputr(void *ms,char *me,FILE *stream)
  {
  int ms_is_parsenode = IS_DLCPARSENODE(ms);
  if (ms_is_parsenode)
    {
    dlc_nodeb *n=ms;
    ms = n->t;
    me = n->eot;
    }
  
  char *mc = ms;
  int ms_is_dlcstring = IS_DLCSTRLIKE(ms);
  dlc_string *mss = (dlc_string *)ms;

  if (ms_is_dlcstring)
    {
    mc = mss->t;
    }
    
  if (me) while ( mc<me ) fputc(*mc++,stream);
  else    while ( *mc   ) fputc(*mc++,stream);
  }

void fputn(void *s,FILE *stream)
  {
  if (! s)
    {
    fputs("NULL",stream);
    return;
    }
  
  dlc_pushupb *p=s;

  while (IS_DLCPUSHUP(s))
    {
    fprintf(stderr,"<PU>");
    s = p->n;
    p = s;
    }
  
  dlc_nodeb *n=s;
  dlc_string *ds=s;
  
  if (IS_DLCPARSENODE(s))   fputr(n->t,n->eot,stream);
  else if (IS_DLCSTRING(s)) fputs(ds->t,stream);
  else                      fputs((char *)s,stream);
  }


char *dlc_strncpy(char *d,char *s,int n)
  {
  char *c = d;

  for ( ; n>1 && *s ; *c++ = *s++ , n-- );
  *c=0;

  return d;
  }

char *dlc_string_dup(void *ds,char *de)
  {
  char *dc = (char *)ds;
  dlc_string *dss = (dlc_string *)ds;
  dlc_nodeb  *dsn = (dlc_nodeb *)ds;

  if (IS_DLCPARSENODE(ds))
    {
    return(strndup(dsn->t,dsn->eot-dsn->t));
    }
  else if (IS_DLCSTRLIKE(ds))
    {
    return(strdup(dss->t));
    }
  else if (de)
    {
    return(strndup(dc,de-dc));
    }
  return(strdup(ds));
  }



char *dlc_string_i(dlc_string **d,int point,char *t, ... )
  {
  va_list ap;

  va_start(ap,t);

  void *l[10];
  int nl=0;
  char c,*p=t,*s=t;

  while(c=*p++)
    {
    if (c=='%' && *p=='%')
      {
      if (s+1<p) dlc_string_ine(d,point,s,p-1);
      if (point>=0) point += p - s;
      dlc_string_ins(d,point,"%");
      if (point>=0) point ++;
      p++;
      s=p;
      }    
    else if (c=='%')
      {
      if (s < p-1)
        {
        dlc_string_ine(d,point,s,p-1);
        if (point>=0) point += p - s -1;
        }
      
      c = *p++;
      s=p;
      if (c>='0' && c<='9')
        {
        c -= '0';
        for ( ; nl<=c ; nl++) { l[nl] = va_arg(ap,void *); }
        int pl = *d ? (*d)->l : 0;
        dlc_string_ins(d,point,l[c]);
        if (point>=0) point += (*d)->l - pl;        
        }
      }
    }
  
  if (s<p) dlc_string_ins(d,point,s);
  va_end(ap);
  return (*d)->t;
  }

/***********************************************************************
 * sl_globmatch:
 *
 * Check if a string matches a globbing pattern.
 *
 * Return 0 if string does not match pattern and non-zero otherwise.
 * 
 * glob patterns:
 *      *       matches zero or more characters
 *      ?       matches any single character
 *      [set]   matches any character in the set
 *      [^set]  matches any character NOT in the set
 *              where a set is a group of characters or ranges. a range
 *              is written as two characters seperated with a hyphen: a-z denotes
 *              all characters between a to z inclusive.
 *      [-set]  set matches a literal hypen and any character in the set
 *
 *      char    matches itself except where char is '*' or '?' or '['
 *      \char   matches char, including any pattern character
 *
 * examples:
 *      a*c             ac abc abbc ...
 *      a?c             acc abc aXc ...
 *      a[a-z]c         aac abc acc ...
 *      a[-a-z]c        a-c aac abc ...
 *
 * http://earthworm.isti.com/trac/earthworm/browser/trunk/src/data_exchange/
 *       slink2ew/libslink/globmatch.c?rev=4078
 * robust glob pattern matcher
 * ozan s. yigit/dec 1994
 * public domain
 *
 **********************************************************************/

char *dlc_string_glob_match_set(char s,char *p)
  {
  /* set specification is inclusive, that is [a-z] is a, z and
   * everything in between. this means [z-a] may be interpreted
   * as a set that contains z, a and nothing in between.
   */
  
  int negate = 0;
  
  if ( *p == '^' )
    {
    negate = 1;
    p++;
    }

  int c;
  while ( (c = *p++) && c != ']')
    {
    if ( c == '\\' )
      {
      c = *p++;
      if (!c) return 0;
      }
    
    if ( *p == '-' )    /* c-c */
      {
      p++;
      int d = *p++;
      if ( !d) return 0;
      if ( d == ']' )
        {           /* c-] */
        if ( s >= c ) return(negate ? 0 : p);
        return(negate ? p : 0);
        }
      else
        {
        if ( s == c || s == d || ( s > c && s < d ) )
          goto dlc_string_glob_match_set_match;
        }
      }
    else                      /* cc or c] */
      {
      if ( c == s )
        goto dlc_string_glob_match_set_match;
      }
    }

  // end of pattern
  return(negate ? p : 0);
  
dlc_string_glob_match_set_match:

  while ( (c = *p++) && c != ']') ;
  return(negate ? 0 : p);
  }


int dlc_string_globmatch(void *sin, void *pin)
  {
  char *s = DLC_GETTEXT(sin);
  char *p = DLC_GETTEXT(pin);

  int d = *s++;
  int c;
  while ( c=*p++ )
    {
    if ( !d && c != '*' ) return 0;
          
    switch ( c )
      {
      case '*':
        while ( *p == '*' ) p++;
              
        c = *p;
        if ( !c ) return 1;

        if ( c != '?' && c != '[' && c != '\\' )
          while ( d && c != d ) d=*s++;
              
        while ( d )
          {
          if ( dlc_string_globmatch(s-1, p) ) return 1;
          d= *s++; 
          }
        return 0;
              
      case '?':
        if ( !d ) return 0;
        break;
        
      case '[':
        p = dlc_string_glob_match_set(d,p);
        if (!p) return 0;
        break;
        
      case '\\':
        c = *p++;
        
      default:
        if ( c != d ) return 0;
        break;
      }
    d = *s++;
    }
	 
  return ! d;
  }

