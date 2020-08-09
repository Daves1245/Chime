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
#include "connection.h"
#include "transmitmsg.h"

#define PORT "33401"
#define BACKLOG 10

#define UPTIME_STR_LEN 10
#define LOGBUFF_STR_LEN 100

volatile sig_atomic_t running = 1;

void sa_handle(int signal, siginfo_t *info, void *ucontext) {
  running = 0;
}

void broadcastmsg(struct message *m);           /* Broadcast msg to all users */
void broadcast(const char *msg, size_t msglen); /* To be deprecated */
void *manager(void *arg);                       /* Manager thread for connections */

struct pollfd listener[BACKLOG]; // connections
int numconns;

/* TODO Non invasive linked list */
struct list_head {
  struct list_head *prev, *next;
};

struct connection *connections;
int next_uid;

void freeconnections(struct connection *c) {
  struct connection *iterator;
  for (iterator = connections; iterator->next != connections; iterator = iterator->next) {
    disconnect(iterator);
  }
    disconnect(iterator);
}
/*
 * name: login_user 
 * params: urequest
 *
 * Check if provided user hints are valid
 * and if so, login the new user.
 */
STATUS login_user(struct connection *entry) {
  printf("logging user with sfd %d\n", entry->sfd);
  struct connection *iterator;
  if (!connections) {
    connections = entry;
    connections->next = connections->prev = entry;
    return entry->uinfo.uid = next_uid++;
  }
  for (iterator = connections; iterator->next != connections; iterator = iterator->next) {
    if (strcmp(iterator->uinfo.handle, entry->uinfo.handle) == 0) {
      return ERROR_USERNAME_IN_USE;
    }
  }
  if (strcmp(iterator->uinfo.handle, entry->uinfo.handle) == 0) {
    return ERROR_USERNAME_IN_USE;
  }

  connections->prev->next = entry;
  entry->prev = connections->prev;
  connections->prev = entry;
  entry->next = connections;
  return OK;
}

/*
 * TODO switch to use connection
 * instead of socket file descriptors
 * (after organization of user info)
 *
 * name: logoff_user
 * params: int sfd
 *
 * remove a user from the current list of 
 * active connections
 */
STATUS logoff_user(int sfd) {
  struct connection *iterator;

  for (iterator = connections->next; iterator != connections; iterator = iterator->next) {
    if (iterator->sfd == sfd) {
      iterator->prev->next = iterator->next;
      iterator->next->prev = iterator->prev;
      free(iterator);
      return OK;
    }
  }

  return ERROR_USER_NOT_FOUND;
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
  sprintf(logbuff, BLUE "BROADCAST (" ANSI_RESET GREEN "%s" ANSI_RESET BLUE "): " ANSI_RESET "%s", m->from, m->txt);
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
  while (running) {
    if (poll(listener, numconns, POLL_TIMEOUT)) {
      for (int i = 0; i < numconns; i++) {
        char logbuff[LOGBUFF_STR_LEN + MAX_TEXT_LEN];
        if (listener[i].fd > 0 && listener[i].revents == POLLIN) {
          recvmessage(listener[i].fd, &m);
          /* Connect a new user */
          if (m.flags == FCONNECT) {
            struct message ret;
            struct connection *conn;

            conn = malloc(sizeof(struct connection));
            if (!conn) {
              fprintf(stderr, "Could not allocate memory for user connection\n");
              /* XXX disconnect all users here */
              _Exit(EXIT_FAILURE);
            }

            memset(conn, 0, sizeof(*conn));
            memset(&ret, 0, sizeof ret);
            strcpy(ret.from, "SERVER");
            timestampmessage(&ret);

            conn->sfd = listener[i].fd;
            conn->next = conn->prev = conn;
            strcpy(conn->uinfo.handle, m.from);
            printf("sfd given to lu: %d\n", conn->sfd);
            STATUS s = login_user(conn);
            if (s == ERROR_USERNAME_IN_USE) {
              sprintf(ret.txt, "A user with the name %s already exists", conn->uinfo.handle);
              ret.flags = ECONNDROPPED;
              sendmessage(conn->sfd, &ret);
              /*
               * TODO when we organize user info later, we will be able to cleanly
               * ask the client for a new username, instead of forcing them to reconnect.
               * but this works for now
               */
              logoff_user(listener[i].fd);
              continue;
            }
          }

          /* 
           * TODO fix this
           * Disconnect a user */
          if (strcmp(m.txt, "/exit") == 0) {
            logoff_user(listener[i].fd);
            listener[i].fd = -1; // remove from poll() query
            sprintf(logbuff, "user %s disconnected", m.from);
            logs(logbuff);
            disconnect_wrapper(listener[i].fd);
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
  int yes = 1; /* Enable SO_REUSEADDR */
  int rv;
  char *port = PORT;
  char logbuff[LOGBUFF_STR_LEN];
  struct sigaction s_act, s_oldact; /* Set signal handlers */
  int res;

  if (argc >= 2) {
    port = argv[2];
  }

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

    /* This explicitly breaks TCP protocol. For now, we want to be able to restart
     * the program quickly, not having to wait for the TIME_WAIT to finish.
     */
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      perror("setsockopt");
      exit(EXIT_FAILURE);
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
    exit(EXIT_FAILURE);
  }

  if (listen(sockfd, BACKLOG) == -1) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  pthread_t managert;
  if (pthread_create(&managert, NULL, manager, NULL)) {
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }

  logs(GREEN "Manager thread created" ANSI_RESET);
  logs("Waiting for connections...");

  while (running) {
    sin_size = sizeof their_addr;
    new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);

    if (new_fd == -1) {
      if (errno == EINTR) {
        logs("Exiting.");
        break; /* Running should be set to 0 */
      }
      perror("accept");
      continue;
    }
    /* TODO bug 1
     * require a connected client to immediately login (give user info for now)
     * or be disconnected. Because the server could be put into a state of indefinitely
     * waiting for a single client before handling new ones, it might be better to send
     * this to a (third?) thread.
     * then we can start organizing all the connected clients to make later features more
     * easy to deal with.
     * */
    listener[numconns].fd = new_fd;
    listener[numconns].events = POLLIN;
    numconns++;
  }

  logs("Disconnecting all users...");
  freeconnections(connections);
  logs("Done.");
  exit(EXIT_SUCCESS);
}
