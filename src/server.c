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

#include "colors.h"
#include "common.h"
#include "message.h"
#include "defs.h"

#define PORT "33401"
#define BACKLOG 10

#define POLL_TIMEOUT 10
#define LOGBUFF_STR_LEN 100

void broadcastmsg(struct message *m);               /* Broadcast msg to all users */
void *manager(void *arg);                           /* Manager thread for connections */

int next_uid;
int showchat = 1;

struct pollfd listener[BACKLOG]; // connections 
int numconns;

/* Disconnect from a client */
void disconnect(int sfd) {

}

void logs(const char *str) {
  time_t rtime;
  struct tm *now;
  time(&rtime);
  now = localtime(&rtime);
  printf(CYAN "[%d:%d]: " ANSI_RESET "%s\n", now->tm_hour, now->tm_min, str);
}

void broadcastmsg(struct message *m) {
  char logbuff[LOGBUFF_STR_LEN + MAX_TEXT_LEN];
  if (showchat) {
    puts(BLUE "BROADCAST:" ANSI_RESET);
    showmessage(m);
  }
  logs(logbuff);
  for (int i = 0; i < numconns; i++) {
    if (listener[i].fd > 0) {
      sendmessage(listener[i].fd, m);
    }
  }
}

void debugmessage(struct message *msg) {
  printf("---MESSAGE---\n");
  printf("[id]: `%d`\n", msg->id);
  printf("[uid]: `%d`\n", msg->uid);
  printf("[timestmp]: `%s`\n", msg->timestmp);
  printf("[from]: `%s`\n", msg->from);
  printf("[txt]: `%s`\n", msg->txt);
  printf("[flags]: `%d`\n", msg->flags);
  printf("---MESSAGE---\n");
}

int register_user(struct user *usr) {
  int ret = next_uid++;
  return ret;
}

void *manager(void *arg) {
  char buff[MAX_RECV_LEN + 1];
  struct message m;
  while (1) {
    if (poll(listener, numconns, POLL_TIMEOUT)) {
      for (int i = 0; i < numconns; i++) {
        char logbuff[LOGBUFF_STR_LEN + MAX_TEXT_LEN];
        if (listener[i].fd > 0 && listener[i].revents == POLLIN) {
          recvmessage(listener[i].fd, &m);
          if (m.flags == FCONNECT) {
            struct message ret;
            struct user newusr;
            strcpy(newusr.handle, m.from);
            register_user(&newusr);
            if (newusr.uid < 0) {
              sprintf(ret.txt, "Username %s already in use\n", newusr.handle);
              ret.flags = ECONNREFUSED;
              sendmessage(listener[i].fd, &ret);
              disconnect(listener[i].fd);
            }
          }
          debugmessage(&m);
          if (strcmp(m.txt, "/exit") == 0) {
            sprintf(logbuff, "Disconnecting client on socket fd: %d", listener[i].fd); // XXX var args logs
            close(listener[i].fd);
            listener[i].fd = -1; // remove from poll() query 
            logs(logbuff);
            sprintf(logbuff, "user %s disconnected", m.from);
            logs(logbuff);
            m.flags = FDISCONNECT;
          }
          broadcastmsg(&m);
          memset(buff, 0, sizeof buff);
        }
      }
    }
  }
  return NULL;
}

int main(int argc, char **argv) {
  int sockfd, new_fd; // listen on sock_fd, new connections on new
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_storage their_addr; // client's address
  socklen_t sin_size;
  int yes = 1;
  char clientip[INET6_ADDRSTRLEN];
  int rv;
  char *port = PORT;

  if (argc >= 2) {
    port = argv[2];
  }

  char logbuff[LOGBUFF_STR_LEN];
  sprintf(logbuff, "Creating server on port %s", PORT); // XXX logs var args
  logs(logbuff);

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
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

  logs(GREEN "Server setup success" ANSI_RESET);
  freeaddrinfo(servinfo); // all done with this structure

  if (p == NULL) {
    fprintf(stderr, "server: failed to bind\n");
    exit(1);
  }

  if (listen(sockfd, BACKLOG) == -1) {
    perror("listen");
    exit(1);
  }

  pthread_t managert;
  if (pthread_create(&managert, NULL, manager, NULL)) {
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }

  logs(GREEN "Manager thread created" ANSI_RESET);
  logs("Waiting for connections...");

  while (1) {
    sin_size = sizeof their_addr;
    new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);

    if (new_fd == -1) {
      perror("accept");
      continue;
    }
    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *) &their_addr), clientip, sizeof clientip);

    listener[numconns].fd = new_fd;
    listener[numconns].events = POLLIN;
    numconns++;
  }
  return 0;
}
