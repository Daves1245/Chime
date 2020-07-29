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

/* TODO change BACKLOG */
struct user usrs_connected_base[BACKLOG];
struct user *usrs_connected;
int num_usrs, usr_capacity = BACKLOG;
int usrs_dynamic;

/* Non invasive linked list */
struct list_head {
  struct list_head *prev, *next;
};

struct online_user {
  struct user usrinfo;
  struct list_head list;
  int sfd;
  char logon_time[UPTIME_STR_LEN];
  int permission_lvl;
};

void logs(const char *str) {
  time_t rtime;
  struct tm *now;
  time(&rtime);
  now = localtime(&rtime);
  printf(CYAN "[%d:%d]: " ANSI_RESET "%s\n", now->tm_hour, now->tm_min, str);
}

struct online_user *usrs_online_list;

// TODO convert to linked list
void add_usr(struct user *usr) {
  if (!usrs_dynamic && num_usrs == usr_capacity) {
    usr_capacity *= 2;
    struct user *tmp = malloc(sizeof(struct user) * usr_capacity);
    if (!tmp) {
      // TODO
      fprintf(stderr, "error allocating memory for new users. Blocking further connections.\n");
    } else {
      usrs_connected = tmp;
      for (int i = 0; i < BACKLOG; i++) {
        memcpy(&usrs_connected[i], &usrs_connected_base[i], sizeof(struct user));
      }
    }
  } else if (num_usrs == usr_capacity) {
    usr_capacity *= 2;
    struct user *tmp = realloc(usrs_connected, sizeof(struct user) * usr_capacity);
    if (!tmp) {
      fprintf(stderr, "error allocating memory for new users. Blocking further connections\n");
    } else {
      usrs_connected = tmp;
    }
  }
}

void add_online_usr(struct user *usr) {
  struct online_user *tmp = malloc(sizeof(struct online_user));
  if (!tmp) {
    fprintf(stderr, RED "error allocating memory for new user\n" ANSI_RESET);
    return;
  }
  tmp->user.uid = usr->uid;
  strcpy(user.handle, usr->handle);
  tmp->head.next = tmp->head.prev = &tmp->head;
  list_add(usrs_online_list->head, tmp);
}

void rm_usr(struct user *usr) {

}

void connect_usr();
void disconnect_usr();

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

  usrs_connected = usrs_connected_base;

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
