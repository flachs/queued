#ifndef _dlc_string_h_
#define _dlc_string_h_

#include <stdlib.h>
#include <stdio.h>

#define DLC_STRING     ('s')
#define DLC_LIST       ('l')
#define DLC_PARSE_NODE ('n')
#define DLC_PUSHUP     ('p')
#define DLC_MALFILE    ('m')
#define DLC_MAPFILE    ('f')
#define DLC_DLB        ('D')
#define DLC_SIGMAP     ('M')


#define DLC_STRING_GUTS   unsigned char m[4]; char *t; size_t s,l,e
#define DLC_NODE_GUTS     unsigned char m[4]; char *t,*eot 
#define DLC_PUSHUP_GUTS   unsigned char m[4]; void *n
#define DLC_DLB_GUTS      unsigned char m[4]
#define DLC_SIGMAP_GUTS   unsigned char m[4]

#define TAG_DLCSTRING(x)     (x->m[0] = -1, x->m[1] = DLC_STRING    )
#define TAG_DLCLIST(x)       (x->m[0] = -1, x->m[1] = DLC_LIST      )
#define TAG_DLCPARSE_NODE(x) (x->m[0] = -1, x->m[1] = DLC_PARSE_NODE)
#define TAG_DLCPUSHUP(x)     (x->m[0] = -1, x->m[1] = DLC_PUSHUP    )
#define TAG_DLCMALFILE(x)    (x->m[0] = -1, x->m[1] = DLC_MALFILE   )
#define TAG_DLCMAPFILE(x)    (x->m[0] = -1, x->m[1] = DLC_MAPFILE   )
#define TAG_DLCDLB(x)        (x->m[0] = -1, x->m[1] = DLC_DLB       )
#define TAG_DLCSIGMAP(x)     (x->m[0] = -1, x->m[1] = DLC_SIGMAP    )

typedef struct
  {
  DLC_STRING_GUTS;
  } dlc_string;

typedef struct
  {
  DLC_NODE_GUTS;
  } dlc_nodeb;

typedef struct
  {
  DLC_PUSHUP_GUTS;
  } dlc_pushupb;

typedef struct
  {
  DLC_STRING_GUTS;
  } dlc_list;

#define IS_DLCSTRING(s)    (((char *)s)[0] == -1 && ((char *)s)[1] == DLC_STRING)
#define IS_DLCPARSENODE(s) (((char *)s)[0] == -1 && ((char *)s)[1] == DLC_PARSE_NODE)
#define IS_DLCDLB(s)       (((char *)s)[0] == -1 && ((char *)s)[1] == DLC_DLB)
#define IS_DLCSIGMAP(s)    (((char *)s)[0] == -1 && ((char *)s)[1] == DLC_SIGMAP)
#define IS_DLCPUSHUP(s)    (((char *)s)[0] == -1 && ((char *)s)[1] == DLC_PUSHUP)
#define IS_DLCFILE(s)      (((char *)s)[0] == -1 && ( ((char *)s)[1] == DLC_MALFILE) || ((char *)s)[1] == DLC_MAPFILE)
#define IS_DLCMAPFILE(s)   (((char *)s)[0] == -1 && ((char *)s)[1] == DLC_MAPFILE)
#define IS_DLCMALFILE(s)   (((char *)s)[0] == -1 && ((char *)s)[1] == DLC_MALFILE)
#define IS_DLCSTRLIKE(s)   (((char *)s)[0] == -1 && ( ((char *)s)[1] == DLC_STRING || ((char *)s)[1] == DLC_MALFILE || ((char *)s)[1] == DLC_MAPFILE))

#define DLC_GETTEXT(s) ((!s) ? "(NULL)" : (IS_DLCSTRLIKE(s)) ? ((dlc_string *)s)->t : (IS_DLCPARSENODE(s)) ? ((dlc_nodeb *)s)->t : (char *)s)

#define DLC_GETLEN(s) ((!s) ? 0 : (IS_DLCSTRLIKE(s)) ? ((dlc_string *)s)->l : (IS_DLCPARSENODE(s)) ? ((dlc_nodeb *)s)->eot-((dlc_nodeb *)s)->t : strlen((char *)s))

typedef enum 
  {
  DLC_LIST_RZ_ABS,
  DLC_LIST_RZ_INC,
  DLC_LIST_RZ_DEC
  } dlc_list_rz_op;

#ifndef _bool_typedef_
#define _bool_typedef_
typedef unsigned char bool ;
#endif

dlc_list *dlc_list_new(dlc_list **tp,size_t needlength);
void     *dlc_list_rz(dlc_list **tp,dlc_list_rz_op op,size_t needlength);
void      dlc_list_free(dlc_list **tp);
void dlc_list_cpy(dlc_list **tp,dlc_list *sp);
void *dlc_list_ne_splice(dlc_list **tp,size_t elemsize,size_t pos,size_t rmn,size_t adn);
void *dlc_list_ne_append(dlc_list **tp,size_t elemsize,size_t net);
void dlc_list_e_delete(dlc_list **tp,size_t pos,size_t net);

#define ListIndex(type,list,index) (((type *)((list)->t))[index])
#define ListIndexP(type,list,index) (&(((type *)((list)->t))[index]))
#define ListAppend(type,list) (type *)dlc_list_ne_append(list,sizeof(type),1)
#define ListAppends(type,list,n) (type *)dlc_list_ne_append(list,sizeof(type),n)
#define ListDelete(list,index) dlc_list_e_delete(list,index,1)
#define ListPos(type,list,pointer) ((pointer) - (type *)((list)->t))
#define ListEntries(list) ((list) ? (list)->l : 0)
#define ListPointer(type,list) ((list) ? (type *)((list)->t) : (type *)0)

dlc_string *dlc_string_new(dlc_string **tp);
dlc_string *dlc_string_rz(dlc_string **tp,size_t needlength);
void        dlc_string_free(dlc_string **tp);
size_t dlc_string_lee(void *s,char *e);
size_t dlc_string_len(void *s);
void dlc_string_cpy(dlc_string **tp,const void *s);
void dlc_string_cpe(dlc_string **tp,char *s,char *e);
void dlc_string_cat(dlc_string **tp,const void *s);
void dlc_string_cep(dlc_string **tp,char *s,char *e);
void dlc_string_ac(dlc_string **tp,int c);
void dlc_string_ins(dlc_string **tp,int point,void *s);
void dlc_string_ine(dlc_string **tp,int point,char *s,char *e);
void dlc_string_rep(dlc_string **tp,int point,int len,void *s);
void dlc_string_rpr(dlc_string **tp,int point,int len,char *s,char *e);
char *dlc_string_i(dlc_string **d,int point,char *t, ... );
int dlc_string_globmatch(void *sin, void *pin);
int dlc_string_has_funny_chars(void *s);

int dlc_rstrcmp(void *ds,char *de,void *ms,char *me);
char *dlc_string_chr(void *ds,char *de,int c);
char *dlc_strncpy(char *d,char *s,int n);
char *dlc_string_dup(void *ds,char *de);

void dlc_strcpe(char *d,char *s,char *e);
void dlc_strcpy(char *d,void *s);
void dlc_strcep(char *d,char *s,char *e);
void dlc_strcat(char *d,void *s);
int dlc_string_caf(dlc_string **tp, const char *format, ...);

#define dlc_string_ptr(ds) ( (IS_DLCSTRLIKE(ds)) ? (((dlc_string *)ds)->t) : (char *)ds)

void fputr(void *ms,char *me,FILE *stream);
void fputn(void *s,FILE *stream);

#endif
