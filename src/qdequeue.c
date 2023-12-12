#include "q.h"
#include "dlc_string.h"
#include "list.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

typedef struct
  {
  pcre2_code *re;
  pcre2_match_data *md;
  job_match_spec_t *jms;
  } re_job_match_spec_t;
  
typedef struct
  {
  joblink_t    *jl;
  sendhdr_t    *hdr;
  parsed_cmd_t pc;
  } jobinfo_t;

typedef void (act_on_job_handler_t)(dlc_string **resp,jobinfo_t *ji);

static inline char *iso_jobdir(char *jd)
  {
  char *rv = strrchr(jd,'/');
  return rv ? rv+1 : NULL;
  }

int jobdir_match(jobinfo_t *ji,re_job_match_spec_t *rjm)
  {
  char *jd = ji->jl->dir;
  char *p = iso_jobdir(jd);
  char *md = rjm->jms->value;  
  
  return !strcmp(p,md);
  }

static inline int jobcmd_match(jobinfo_t *ji,re_job_match_spec_t *rjm)
  {
  return !!(strstr(ji->pc.cmd,rjm->jms->value));
  }

static inline int jobgrp_match(jobinfo_t *ji,re_job_match_spec_t *rjm)
  {
  return ji->jl->jg == rjm->jms->jg;
  }


int jobhost_match(jobinfo_t *ji,re_job_match_spec_t *rjm)
  {
  hostlink_t *h = ji->jl->h;
  char *host = rjm->jms->value;
  
  if (host[0] == '-' && host[1] == '0') return h==0;
  
  if (! h ) return 0;

  printf("jhm: <%s> <%s>\n",h->host,host);
  if (! strcmp(h->host,host)) return 1;
  
  return 0;
  }

int jobcmd_rematch(jobinfo_t *ji,re_job_match_spec_t *rjm)
  {
  char *cmd = ji->pc.cmd;
  int match = pcre2_match(rjm->re,cmd,strlen(cmd),
                          0,0,rjm->md,NULL);
  return match;
  }

int job_in_timerange(jobinfo_t *ji,re_job_match_spec_t *rjm)
  {
  time_t beg = rjm->jms->beg;
  time_t end = rjm->jms->end;
  
  joblink_t *jl = ji->jl;
  int ab = jl->ct.tv_sec >= beg;
  int be = jl->ct.tv_sec <= end;
  int rv = ab && be;
  return rv;
  }

static inline int jobuid_match(jobinfo_t *ji,re_job_match_spec_t *rjm)
  {
  joblink_t *jl = ji->jl;
  job_match_spec_t *jms = rjm->jms;
  return jms->uid == (uid_t)(-1) || jl->u->uid == jms->uid;
  }

int job_match(jobinfo_t *ji,re_job_match_spec_t *rjm)
  {
  job_match_spec_t *jms = rjm->jms;
  jms_spec_t spec = jms->spec;
  
  if (spec & JMS_ALL) return 1;

  if ((spec & JMS_TIME)   && !job_in_timerange(ji,rjm)) return 0;
  if (                       !jobuid_match(ji,rjm))     return 0;
  if ((spec & JMS_JOBDIR) && !jobdir_match(ji,rjm))     return 0;
  if ((spec & JMS_CMDSS ) && !jobcmd_match(ji,rjm))     return 0;
  if ((spec & JMS_CMDRE ) && !jobcmd_rematch(ji,rjm))   return 0;
  if ((spec & JMS_JG    ) && !jobgrp_match(ji,rjm))     return 0;
  if ((spec & JMS_HOST  ) && !jobhost_match(ji,rjm))     return 0;

  return 1;
  }

void act_on_job_in_ul(uidlink_t *ul,
                      re_job_match_spec_t *rjm,
                      dlc_string **response,
                      act_on_job_handler_t handler)
  {
  jobinfo_t ji;
    
  for (ji.jl=ul->head; ji.jl ; ji.jl = ji.jl->un)
    {
    ji.hdr = get_job_cmd(ji.jl->dir,&ji.pc);
    if (! ji.hdr) continue;  // partially written -- doesnt count
    
    if (job_match(&ji,rjm))
      (handler)(response,&ji);
    
    free(ji.pc.env);
    free(ji.hdr);
    }
  
  return;
  }

void act_on_job(uid_t uid,job_match_spec_t *jms,
                dlc_string **response,
                act_on_job_handler_t handler)
  {
  joblink_t *jl = 0;
  int priv = uid == 0;
  pcre2_code *re=0;
  re_job_match_spec_t rjm;
  rjm.re  = 0;
  rjm.md  = 0;
  rjm.jms = jms;
  
  if (jms->spec & JMS_CMDRE)
    {
    rjm.re = pcre2_compile( jms->value,
                            PCRE2_ZERO_TERMINATED,
                            0, /* default options */
                            NULL,/* for error number */
                            NULL,/* for error offset */
                            NULL);
    rjm.md = pcre2_match_data_create_from_pattern(rjm.re, NULL);
    }
  
  if (priv && jms->uid == (uid_t)(-1))
    { // root can look at all uid
    uidlink_t *ul = uidlist_head();
    for ( ; ul ; ul = ul->un ) //foreach user
      act_on_job_in_ul(ul,&rjm,response,handler);
    }
  else
    {
    uidlink_t *ul = find_uid(priv ? jms->uid : uid);
    if (ul) act_on_job_in_ul(ul,&rjm,response,handler);
    }
  
  if (rjm.re)
    {
    pcre2_match_data_free(rjm.md); /* Release memory used for the match */
    pcre2_code_free(rjm.re);       /* data and the compiled pattern. */
    }
  }


int dequeue_job(joblink_t *jl)
  {
  if (!jl) return ENOKEY;

  hostlink_t *hl = jl->h;
  if (!hl)
    { // not running -- just unlink it
    remove_link(jl,h);  // unlink it from host list
    remove_link(jl,u);  // unlink it from uid list
    update_job_dir_when_done(jl,-1);
    return 0;
    }
  
  // it is on host - need to kill it
  if (strcmp(hl->host,hostname()))
    { // remote host - send message to terminate it
    if (0)
      fprintf(stderr,"   dj-remote: %p %d\n",
              jl,jl->u->uid);
    return send_kill(hl->host,jl);
    }

  // local host -- send a signal to terminate it
  if (0) fprintf(stderr,"   dj-local: \n");
  return kill_job(jl->tag);
  }

void dequeue_a_job(dlc_string **resp,jobinfo_t *ji)
  {
  joblink_t *jl = ji->jl;
  int er=dequeue_job(jl);
  if (er)
    dlc_string_caf(resp,"dequeue of %s failed %s\n",
                   jl->dir,
                   strerror(er));
  else
    dlc_string_caf(resp,"dequeue of %s suceeded\n",
                   jl->dir);
  }

void recv_acton_reply(server_thread_args_t *client,
                      act_on_job_handler_t action)
  {
  dlc_string *response=0;
  sendhdr_t hdr = client->hdr;
  int argn = hdr.value[0];
  char buf[hdr.size];
  job_match_spec_t *jms=(job_match_spec_t *)buf;
  
  recvn(client->sock,buf,hdr.size,0);  // have args

  act_on_job(hdr.uid,jms,&response,action);
  
  hdr.size = dlc_string_len(response);
  if (hdr.size>0) hdr.size ++;
  
  hdr.kind = DK_reply;
  send_response(client->sock,&hdr,hdr.size ? response->t : NULL);
  dlc_string_free(&response);
  
  close(client->sock);
  }

// dequeue request... returns string to client in
// enqueue.c: dequeue_client
void server_dequeue(server_thread_args_t *client)
  {
  recv_acton_reply(client,dequeue_a_job);
  }

void list_a_job(dlc_string **resp,jobinfo_t *ji)
  {
  joblink_t *jl = ji->jl;
  char tb[200];
  
  dlc_string_caf(resp,"%s %s %s %d %s %s\n",
                 jl->u->name,
                 jl->h ? jl->h->host : "-",
                 format_time(tb,jl->ct.tv_sec),
                 jl->jg,
                 iso_jobdir(jl->dir),
                 ji->pc.cmd);
  }

// list jobs request... returns string to client in
// enqueue.c: listqueue_client
void server_list(server_thread_args_t *client)
  {
  recv_acton_reply(client,list_a_job);
  }

