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

#define UPTIME_STR_LEN 10
#define LOGBUFF_STR_LEN 100

void broadcastmsg(struct message *m);           /* Broadcast msg to all users */
void broadcast(const char *msg, size_t msglen); /* To be deprecated */
void *manager(void *arg);                       /* Manager thread for connections */

struct pollfd listener[BACKLOG]; // connections
int numconns;

/* Non invasive linked list */
struct list_head {
  struct list_head *prev, *next;
};

struct connection {
  int sfd;                        /* Socket on which usr is connected */
  struct user uinfo;              /* User info */
  struct connection *next, *prev; /* Make this a linked list */
};

struct connection *connections;

int next_uid;

/*
 * name: register_user
 * params: uhints
 *
 * Check if provided user hints are valid
 * and if so, register the new user. Returns
 * negative on invalid user data and sets
 * the user uid to -1
 *
 * RETURN VALUE:
 * positive uid if valid user info, -1 on
 * invalid.
 */
int register_user(struct user *uhints) {
  struct connection *iterator, *conn;
  printf("%d\n", connections->uinfo.uid);
  if (connections->uinfo.uid == -1) {
    memcpy(&connections->uinfo, uhints, sizeof(*uhints));
    return connections->uinfo.uid = next_uid++;
  }
  for (iterator = connections; iterator && iterator->next != connections; iterator = iterator->next) {
    if (strcmp(iterator->uinfo.handle, uhints->handle) == 0) {
      break;
    }
  }
  if (iterator->next == connections) {
    return uhints->uid = -1;
  }
  conn = malloc(sizeof(*conn));
  memset(conn, 0, sizeof(*conn));
  if (!conn) {
    fprintf(stderr, "Could not allocate space for new connection\n");
    perror("mallloc");
    _Exit(EXIT_FAILURE);
  }
  connections->prev->next = conn;
  conn->prev = connections->prev;
  connections->prev = conn;
  conn->next = connections;
  memcpy(&conn->uinfo, uhints, sizeof(*uhints));
  return conn->uinfo.uid = next_uid++;
}

int logoff_user(int sfd) {
  return 1;
}

void disconnect(int sfd) {
  struct message fin;
  memset(&fin, 0, sizeof fin);
  fin.flags = FDISCONNECT;
  sendmessage(sfd, &fin);
  close(sfd);
}

void logs(const char *str) {
  time_t rtime;
  struct tm *now;
  time(&rtime);
  now = localtime(&rtime);
  printf(CYAN "[%d:%d]: " ANSI_RESET "%s\n", now->tm_hour, now->tm_min, str);
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
  }
}

void broadcastmsg(struct message *m) {
  char logbuff[LOGBUFF_STR_LEN + MAX_TEXT_LEN];
  sprintf(logbuff, BLUE "BROADCAST: " ANSI_RESET "%s", m->txt);
  logs(logbuff);
  for (int i = 0; i < numconns; i++) {
    if (listener[i].fd > 0) {
      sendmessage(listener[i].fd, m);
    }
  }
}

void *manager(void *arg) {
  char buff[MAX_RECV_LEN + 1];
  struct message m;
  while (1) {
    if (poll(listener, numconns, 1)) {
      for (int i = 0; i < numconns; i++) {
        char logbuff[LOGBUFF_STR_LEN + MAX_TEXT_LEN]; // XXX make logs var args. tmp hack fix
        if (listener[i].fd > 0 && listener[i].revents == POLLIN) {
          recvmessage(listener[i].fd, &m);

          /* Connect a new user */
          if (m.flags == FCONNECT) {
            printf("connecting client\n");
            struct message ret;
            struct connection *conn;

            memset(&ret, 0, sizeof ret);
            conn = malloc(sizeof(struct connection));

            if (!conn) {
              fprintf(stderr, "Could not allocate memory for user connection\n");
              /* XXX disconnect all users here */
              _Exit(EXIT_FAILURE);
            }

            conn->sfd = listener[i].fd;
            strcpy(conn->uinfo.handle, m.from);
            register_user(&conn->uinfo);
            if (conn->uinfo.uid < 0) {
              sprintf(ret.txt, "A user with the name %s already exists\n", conn->uinfo.handle);
              ret.flags = ECONNREFUSED;
              sendmessage(conn->sfd, &ret);
              disconnect(conn->sfd);
            }
          }

          /* Disconnect a user */
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
    } else {
      continue;
    }
  }
  return NULL;
}

int main(int argc, char **argv) {
  int sockfd, new_fd; // listen on sock_fd, new connections on new
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_storage their_addr; // connect's address
  socklen_t sin_size;
  int yes = 1;
  char s[INET6_ADDRSTRLEN];
  int rv;
  char *port = PORT;
  char logbuff[LOGBUFF_STR_LEN];

  if (argc >= 2) {
    port = argv[2];
  }

  connections = malloc(sizeof(*connections));
  connections->uinfo.uid = -1;
  connections->next = connections->prev = connections;

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
    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *) &their_addr), s, sizeof s);

    listener[numconns].fd = new_fd;
    listener[numconns].events = POLLIN;
    numconns++;
  }
  return 0;
}
