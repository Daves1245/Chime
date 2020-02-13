#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <poll.h>

#include "common.h"

#define PORT "33401"
#define MAXDATASIZE 100

int main(int argc, char **argv) {

  char handle[MAX_NAME_LEN + 1];
  printf("handle:");
  scanf("%s", handle);

  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  char s[INET6_ADDRSTRLEN];

  if (argc != 2) {
    fprintf(stderr, "usage: client [HOSTNAME]\n");
    exit(1);
  }

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if ((rv = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  // loop through all the results and connect to the first we can
  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("client: socket");
      continue;
    }
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("client: connect");
      continue;
    }
    break;
  }
  if (p == NULL) {
    fprintf(stderr, "client: failed to connect\n");
    return 2;
  }

  inet_ntop(p->ai_family, get_in_addr((struct sockaddr *) p->ai_addr), s, sizeof s);
  printf("connecting to %s\n", s);
  freeaddrinfo(servinfo); // all done with this structure 

  /* Tell the server who we are */
  struct message msg;
  memset(&msg, 0, sizeof msg);
  memcpy(msg.from, handle, sizeof handle);
  trysend(sockfd, &msg, sizeof msg);

  pthread_t handler;
  struct handlerinfo info;
  info.sfd = sockfd;
  info.handle = handle;

  if (pthread_create(&handler, NULL, connection_handler, &info)) {
    fprintf(stderr, "Could not create handler for connection\n");
    perror("perror_create");
    exit(EXIT_FAILURE);
  }

  /* Join handler with main on termination, then exit */
  pthread_join(handler, NULL);

  close(sockfd);
  return 0;
}
