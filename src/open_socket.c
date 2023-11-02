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
    ssize_t tb = recv(sock,b,len,flags);
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

  int server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
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
  struct protoent *protoent = getprotobyname("tcp");
  if (protoent == NULL)
    {
    perror("getprotobyname");
    abort();
    }
    
  int sockfd = socket(AF_INET, SOCK_STREAM, protoent->p_proto);
  if (sockfd == -1)
    {
    perror("socket");
    abort();
    }

  struct sockaddr_in sockaddr_in;
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

int open_client_socket(const char *serverspec,int server_port,const char *msg)
  {
  char *server_hostname = STRDUPA(serverspec);
  char *server_portnum  = strchr(server_hostname,':');
  
  if (server_portnum) *server_portnum++ = 0;

  struct addrinfo *res;
  if (getaddrinfo(server_hostname,server_portnum,
                  NULL,&res))
    {
    fprintf(stderr, "error: gethostbyname(\"%s\")\n", server_hostname);
    return -1;
    }

  struct sockaddr_in *sa = (struct sockaddr_in *)(res->ai_addr);
  in_addr_t in_addr = sa->sin_addr.s_addr;

  freeaddrinfo(res);

  int sockfd = connected_socket(in_addr,server_port);

  if (sockfd<1 && msg)
    {
    fprintf(stderr,"%s: %s\n",msg,strerror(errno));
    }
  
  return sockfd;
  }

