#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>

#include "common.h"

#define PORT "33401"
#define BACKLOG 10

void sigchld_handler(int);                      /* Error handling */
void broadcastmsg(struct message *m);               /* Broadcast msg to all users */
void broadcast(const char *msg, size_t msglen); /* To be deprecated */
void *manager(void *arg);                       /* Manager thread for connections */

struct pollfd listener[BACKLOG]; // connections 
int numconns;

void sigchld_handler(int s) {
  // waitpid() might overwrite errno, so we save and restore it
  int saved_errno = errno;
  while (waitpid(-1, NULL, WNOHANG) > 0);
  errno = saved_errno;
}

/* Broadcast a message to all users */
void broadcast(const char *msg, size_t msglen) {
  size_t numbytes;
  for (int i = 0; i < numconns; i++) {
    if (listener[i].fd > 0) {
      if ((numbytes = send(listener[i].fd, msg, msglen, 0)) != msglen) {
        if (numbytes < 0) {
          perror("send");
          break;
        } else {
          perror("incomplete send");
          break;
        }
      }
    }
  } // end of loop
}

void broadcastmsg(struct message *m) {
  for (int i = 0; i < numconns; i++) {
    if (listener[i].fd > 0) {
      sendmessage(listener[i].fd, m);
    }
  } 
}

void *manager(void *arg) {
  char buff[MAX_RECV_LEN];
  struct message m;
  while (1) {
    poll(listener, numconns, 0);
    for (int i = 0; i < numconns; i++) {
      if (listener[i].fd > 0 && listener[i].revents == POLLIN) {
        recvmessage(listener[i].fd, &m);
        broadcastmsg(&m);
        /*size_t numbytes;
          if ((numbytes = recv(listener[i].fd, buff, MAX_RECV_LEN, 0)) == -1) {
          perror("recv");
          break;
          }*/
        //broadcast(buff, numbytes);
        printf("[BROADCAST]:%s\n", m.text);
        memset(buff, '\0', MAX_RECV_LEN);
      }
    } // end msg searching loop 
  }
  return NULL;
}

int main(int argc, char **argv) {
  int sockfd, new_fd; // listen on sock_fd, new connections on new
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_storage their_addr; // connect's address
  socklen_t sin_size;
  struct sigaction sa;
  int yes = 1;
  char s[INET6_ADDRSTRLEN];
  int rv;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  // loop through all the results and bind to the first we can
  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("server: socket");
      continue;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      perror("setsockopt");
      exit(1);
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("server: bind");
      continue;
    }

    break;
  }

  freeaddrinfo(servinfo); // all done with this structure

  if (p == NULL) {
    fprintf(stderr, "server: failed to bind\n");
    exit(1);
  }

  if (listen(sockfd, BACKLOG) == -1) {
    perror("listen");
    exit(1);
  }

  sa.sa_handler = sigchld_handler; // reap all dead procedures
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  pthread_t managert;
  if (pthread_create(&managert, NULL, manager, NULL)) {
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }

  printf("server: waiting for connections...\n");
  while (1) { // main accept() loop
    sin_size = sizeof their_addr;
    new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);

    if (new_fd == -1) {
      perror("accept");
      continue;
    }

    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *) &their_addr), s, sizeof s);
    printf("Connection from %s\n", s);

    listener[numconns].fd = new_fd;
    listener[numconns].events = POLLIN;
    numconns++;
  }
  return 0;
}
