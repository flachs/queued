#include "q.h"

void server_restart(server_thread_args_t *client)
  {
  extern volatile int server_restart_req;

  server_restart_req = 1;
  
  sendhdr_t hdr = client->hdr;
  int sock = client->sock;

  hdr.kind = DK_echorep;
  hdr.size = 0;
  
  send_response(sock,&hdr,NULL);
  }

void server_reload(server_thread_args_t *client)
  {
  extern volatile int reload_conf;

  reload_conf = 1;
  
  sendhdr_t hdr = client->hdr;
  int sock = client->sock;

  hdr.kind = DK_echorep;
  hdr.size = 0;
  
  send_response(sock,&hdr,NULL);
  }

void server_terminate(server_thread_args_t *client)
  {
  extern volatile int server_restart_req;

  server_restart_req = 2;
  
  sendhdr_t hdr = client->hdr;
  int sock = client->sock;

  hdr.kind = DK_echorep;
  hdr.size = 0;
  
  send_response(sock,&hdr,NULL);
  }

int terminator(conf_t *conf,const char *hostname,void *p)
  {
  datakind_t kind = (datakind_t)(p);
  
  int port = getserviceport();
  char *msg = kind==DK_reload    ? "reloading" :
              kind==DK_restart   ? "restarting" :
              kind==DK_terminate ? "terminating" : NULL;

  if (! msg)
    {
    printlog("terminator: unknown operation %d\n",kind);
    return 1;
    }
  
  printlog("%s %s\n",msg,hostname);
  
  sendhdr_t hdr;
  if ( get_magic_for_host(hostname,&hdr.magic) ) return 0;
  hdr.uid   = getuid();
  hdr.gid   = getgid();
  hdr.kind  = kind;
  hdr.size  = 0;

  int sockfd = open_client_socket(hostname,port,__func__);
  if (sockfd<0) return 0;
  
  send(sockfd,&hdr,sizeof(hdr),0);

  recvn(sockfd,&hdr,sizeof(hdr),0);

  close(sockfd);
  return 0;
  }


int restart_client(conf_t *conf,int argn,char **argv,char **env)
  {
  char *host = dlc_option_value(NULL,"restart");
  return for_each_host(conf,host,
                       terminator,(void *)DK_restart);
  
  }

int terminate_client(conf_t *conf,int argn,char **argv,char **env)
  {
  char *host = dlc_option_value(NULL,"term");
  
  return for_each_host(conf,host,
                       terminator,(void *)DK_terminate);
  }

int reload_client(conf_t *conf,int argn,char **argv,char **env)
  {
  char *host = 0;
  if (argn>0)
    {
    host = *argv++;
    argn--;
    }
  else
    host="master";

  return for_each_host(conf,host,terminator,(void *)DK_reload);
  }
