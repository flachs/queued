#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#define STRDUPA(s) strcpy(alloca(strlen(s)),s)

size_t recvn(int sock,void *b,size_t len,int flags)
  {
  size_t bytes = 0;
  while (len>0)
    {
    ssize_t tb = recv(sock,b+bytes,len,flags);
    if (tb<0)
      {
      if (bytes) return(bytes);
      return tb;
      }
    len -= tb;
    bytes += tb;
    }
  return bytes;
  }

int get_sock_port(int sockfd)
  {
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  
  getsockname(sockfd,(struct sockaddr *)&addr,&addrlen);
  return ntohs(addr.sin_port);
  }

int open_server_socket(int port)
  {
  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_port = htons(port);

  int server_sockfd = socket(AF_INET, SOCK_STREAM, 6);// /etc/protocols:TCP=6
  int option=1;
  setsockopt(server_sockfd,SOL_SOCKET,SO_REUSEPORT,&option,sizeof(option));
  
  if (server_sockfd<0)
    {
    perror("socket");
    exit(1);
    }

  if (bind(server_sockfd,
           (struct sockaddr *)&server_address,
           sizeof(server_address))<0)
    {
    perror("bind");
    exit(1);
    }
  
  if (listen(server_sockfd, 5)<0)
    {
    perror("listen");
    exit(1);
    }
  return server_sockfd;
  }


int connected_socket(in_addr_t in_addr,int server_port)
  {
  // getprotobyname is unreliable.  Just use the value 6
  int sockfd = socket(AF_INET, SOCK_STREAM, 6); // /etc/protocols:TCP=6
  if (sockfd == -1)
    {
    perror("socket");
    abort();
    }

  struct sockaddr_in sockaddr_in;
  memset(&sockaddr_in,0,sizeof(sockaddr_in));

  sockaddr_in.sin_addr.s_addr = in_addr;
  sockaddr_in.sin_family = AF_INET;
  sockaddr_in.sin_port = htons(server_port);

  if (connect(sockfd,
              (struct sockaddr*)&sockaddr_in,
              sizeof(sockaddr_in)) == -1)
    {
    close(sockfd);
    return -1;
    }
  return sockfd;
  }

typedef struct addrce_s
  {
  struct addrce_s *n;
  in_addr_t a;
  char name[];
  } addrce_t;

addrce_t *in_addr_h=0;

in_addr_t get_in_addr(char *name)
  {
  for (addrce_t *p=in_addr_h;p;p=p->n)
    if (!strcmp(p->name,name)) return p->a;

  
  struct addrinfo *res;
  if (getaddrinfo(name,NULL,NULL,&res))
    {
    fprintf(stderr, "error: gethostbyname(\"%s\")\n", name);
    return INADDR_NONE;
    }
  addrce_t *n = malloc(sizeof(addrce_t)+strlen(name)+1);
  strcpy(n->name,name);
  struct sockaddr_in *sa = (struct sockaddr_in *)(res->ai_addr);
  n->a = sa->sin_addr.s_addr;
  n->n = in_addr_h;
  in_addr_h = n;
  freeaddrinfo(res);
  return n->a;
  }

  
int open_client_socket(const char *serverspec,int server_port,const char *msg)
  {
  char *server_hostname = STRDUPA(serverspec);
  char *server_portnum  = strchr(server_hostname,':');
  
  if (server_portnum) *server_portnum++ = 0;

  in_addr_t in_addr = get_in_addr(server_hostname);

  int sockfd = connected_socket(in_addr,server_port);

  if (sockfd<1 && msg)
    {
    fprintf(stderr,"%s: %s\n",msg,strerror(errno));
    }
  
  return sockfd;
  }

