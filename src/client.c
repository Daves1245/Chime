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
#include "message.h"
#include "colors.h"
#include "defs.h"
#include "threading.h"

#define LOCALHOST "127.0.0.1"
#define PORT "33401"
#define MAXDATASIZE 100

// TODO standard signal error handling
void sa_handle(int signal, siginfo_t *info, void *ucontext) {
    connected = 0;
}

int main(int argc, char **argv) {
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  char serverip[INET6_ADDRSTRLEN];
  struct user usr;
  struct handlerinfo info;
  char *hostname = LOCALHOST;
  struct sigaction s_act, s_oldact;
  int res;

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
  inet_ntop(p->ai_family, get_in_addr((struct sockaddr *) p->ai_addr), serverip, sizeof serverip);

  printf(GREEN "Connected to %s\n" ANSI_RESET, serverip);
  freeaddrinfo(servinfo); // all done with this structure 

  /* Implement signal handling */
  s_act.sa_sigaction = sa_handle;
  s_act.sa_flags = SA_SIGINFO;
  res = sigaction(SIGTERM, &s_act, &s_oldact);
  if (res != 0) {
      perror("sigaction:");
  }
  res = sigaction(SIGINT, &s_act, &s_oldact);
  if (res != 0) {
      perror("sigaction:");
  }

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

  /* Wait for threads to terminate and exit */
  pthread_join(sendertid, NULL);
  pthread_join(receivertid, NULL);

  if (!connected) {
    printf("Disconnected from %s\n", serverip);
  } else {
    fprintf(stderr, "error: threads terminated before connection closed\n");
  }
  close(sockfd);
  return 0;
}

