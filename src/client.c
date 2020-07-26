#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

#ifdef __unix__
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>

#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "common.h"
#include "message.h"
#include "colors.h"
#include "defs.h"
#include "threading.h"

#define LOCALHOST "127.0.0.1"
#define PORT "33401"
#define MAXDATASIZE 100

void sigterm_handler(int s) {
  if (s == SIGTERM) {
    endconnection();
  }  
}

int main(int argc, char **argv) {
#ifndef __unix__
  WSADATA wsa;
  SOCKET sock;
#endif
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  char s[INET6_ADDRSTRLEN];
  struct user usr;
  struct handlerinfo info;
  char *hostname = LOCALHOST;
  
  /* Windows requires winsocket initialization */
#ifndef __unix__
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    fprintf(stderr, "Could not initialize winsocket. Error: %d\n", WSAGetLastError());
    return EAGAIN;
  }
  printf("Winsock initialized.\n");
#endif

  if (argc > 1) {
    hostname = argv[1];
  }

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if ((rv = getaddrinfo(hostname, PORT, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  printf("Attempting to connect...\n");
  // loop through all the results and connect to the first we can
  for (p = servinfo; p != NULL; p = p->ai_next) {
#ifdef __unix__
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("client: socket");
      continue;
    }
#else
    if ((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == INVALID_SOCKET) {
      fprintf(stderr, "could not create socket. Error: %d\n", WSAGetLastError());
      return EAGAIN;
    }
#endif
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
  printf(GREEN "Connected to %s\n" ANSI_RESET, s);
  freeaddrinfo(servinfo); // all done with this structure 

  /* So that ctrl-c doesn't break everything */
  signal(SIGTERM, sigterm_handler);

  /* Tell the server who we are */
  printf("handle:");
  scanf("%s", usr.handle);

  info.sfd = sockfd;
  info.handle = usr.handle;
  info.usr = &usr;

  pthread_t sendertid;
  pthread_t receivertid;

  if (pthread_create(&sendertid, NULL, thread_send, &info)) {
    fprintf(stderr, "Could not create msg sender thread\n");
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }

  if (pthread_create(&receivertid, NULL, thread_recv, &info)) {
    fprintf(stderr, "Could not create msg receiving thread\n");
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }

  /* Join threads (end the connection) to main, and exit */
  pthread_join(sendertid, NULL);
  pthread_join(receivertid, NULL);

  close(sockfd);
  return 0;
}

