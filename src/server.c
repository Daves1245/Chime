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
#include <fcntl.h>
#include <unistd.h>

#include "colors.h"
#include "common.h"
#include "message.h"
#include "defs.h"
#include "connection.h"
#include "transmitmsg.h"
#include "signaling.h"

#define PORT "33401"
#define BACKLOG 10
#define FILEBUFF_LEN 1000 // TODO find optimization for this
#define FILENAME_LEN 100
#define MODE 0666 // TODO mv to defs

#define UPTIME_STR_LEN 10
#define LOGBUFF_STR_LEN 100

/* Each file sent is prepended by this so that the recipient knows what to expect */
struct fileheader {
  char filename[FILENAME_LEN + 1];
  size_t size;
};

FILE *outputstream;

void broadcastmsg(struct message *m);           /* Broadcast msg to all users */
void broadcast(const char *msg, size_t msglen); /* To be deprecated */
void *manager(void *arg);                       /* Manager thread for connections */

/* Main loop variable */
volatile sig_atomic_t running = 1;

/* Pointer to linked list of currently connected users */
struct connection *connections;
/* The uid to assign to the next connected user */
int next_uid;

/* Array of pollfd structures used to find out if a user has sent a message
 * or is ready to receive a message */
struct pollfd listener[BACKLOG];
/* The number of currently connected users */
int numconns;

/* Signal handler to gracefully exit on SIGINT or SIGTERM */
void sa_handle(int signal, siginfo_t *info, void *ucontext) {
  running = 0;
}

/* TODO Non invasive linked list or array */
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
  struct connection *iterator;
  if (!connections) {
    connections = entry;
    connections->next = connections->prev = entry;
    return entry->uinfo.uid = next_uid++;
  }
  for (iterator = connections; iterator->next != connections; iterator = iterator->next) {
    printf("handle: %s\n", iterator->uinfo.handle);
    // TODO if (iterator->uinfo.uid == entry->uinfo.uid) {return ERROR_ALREADY_CONNECTED;}
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
 * active connections.
 * 
 * Return value: ERROR_USER_NOT_FOUND if
 * the requested user could not be found,
 * OK on success.
 */
STATUS logoff_user(int sfd) {
  struct connection *iterator = connections;
  int flag = 0;
  while (!flag || iterator != connections) {
    flag |= iterator == connections;
    if (iterator->sfd == sfd) {
      if (flag) {
        connections = connections->next;
      }
      iterator->prev->next = iterator->next;
      iterator->next->prev = iterator->prev;
      free(iterator);
      return OK;
    }
    iterator = iterator->next;
  }
  return ERROR_USER_NOT_FOUND;
}

/*
 * name: logs
 * params: string
 *
 * Log a message to the output stream
 * (stdout by default)
 */
void logs(const char *str) {
  time_t rtime;
  struct tm *now;
  time(&rtime);
  now = localtime(&rtime);
  fprintf(outputstream, CYAN "[%d:%02d]: " ANSI_RESET "%s\n", now->tm_hour, now->tm_min, str);
}

/*
 * name: broadcast
 * params: string msg, size msglen
 *
 * (DEPRECATED) broadcast a string to all users
 */
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

/*
 * name: broadcastmsg
 * params: struct message pointer
 *
 * Broadcast a message to all clients currently connected
 */
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

/* PSEUDO PROTOTYPE FILE MANAGING */

int min(int a, int b) {
  return a < b ? a : b;
}

/*
 * name: uploadmanager
 * params: file descriptor outfd, connection pointer conn, fileheader pointer filei
 *
 * This thread sends a file to the client associated with conn
 *
 * Return type: TBD
 */
void *uploadmanager(int outfd, struct connection *conn, struct fileheader *filei) {
  char buff[FILEBUFF_LEN];
  size_t received = 0, written = 0, tmp, bufflen;

  while (received < filei->size) {
    tmp = recv(conn->sfd, buff, min(sizeof(buff), filei->size - received), 0);
    if (tmp < 0) {
      perror("[uploadmanager] recv:");
      return NULL; // TODO
    }
    if (tmp == 0) {
      logs(YELLOW "[uploadmanager]:" RED " Connection to client dropped before file could be transferred.\n" ANSI_RESET);
      return NULL; // XXX return ERR_LOST_CONN
    }

    received += tmp;
    bufflen = tmp;

    while (written < bufflen) {
      tmp = write(outfd, buff + written, sizeof(buff) - written);
      if (tmp < 0) {
        if (errno == EINTR) {
          continue; // try again
        }
      }
      if (tmp == 0) {
        fprintf(stderr, "[uploadmanager]: write returned 0\n");
        perror("write");
        return NULL; // TODO return ERR_FAILED_SYS
      }
      written += tmp;
    }
  }
  return NULL; // TODO
}

/*
 * name: parsefileheader
 * params: connection pointer conn, filehader pointer dest
 *
 * Fills the dest pointer with the necessary information grabbed
 * from conn after an FUPLOAD request
 *
 * Return value: OK on success. If any of the system calls open,
 * recv fail, then ERROR_FAILED_SYSCALL is returned.
 */
STATUS parsefileheader(const struct connection *conn, struct fileheader *dest) {
  int outfd, parsed = 0;
  //struct fileheader header;
  // TODO recv filehader, parse filename and info

  while ((outfd = open(dest->filename, O_CREAT | O_WRONLY, MODE)) == -1) {
    if (errno == EINTR) {
      continue;
    }
    return ERROR_FAILED_SYSCALL;
  }
  while (!parsed) {
    parsed = 1; // XXX finish
  }
  return OK;
}

/*
 * name: filemanager
 * params: connection pointer conn, fileheader pointer fi
 *
 * 
 */
STATUS filemanager(struct connection *conn, struct fileheader *fi) {
  char buff[FILEBUFF_LEN];
  //if (upload) {
    int outfd, received, written, temp;
start:
    if ((outfd = open(fi->filename, O_CREAT | O_WRONLY, 0666)) == -1) {
      if (errno == EINTR) {
        goto start;      
      }
      perror("open");
      return ERROR_FAILED_SYSCALL;
   // }

    while (received < fi->size) {
      memset(buff, 0, sizeof buff);
      temp = recv(conn->sfd, buff, sizeof(buff), 0);
      if (temp == 0) {
        logs("Could not receive file: connection closed");
        return ERROR_CONNECTION_DROPPED;
      }
      if (temp < 0) {
        perror("recv");
        exit(EXIT_FAILURE); // TODO research: is this fatal? should it stop the program?
      }
      received += temp;
      int bufflen = temp;
      while (written < bufflen) {
        temp = write(outfd, buff + written, bufflen - written);
        if (temp < 0) {
          perror("write");
          exit(EXIT_FAILURE);
        }
        if (temp == 0) {
          fprintf(stderr, "write wrote 0\n");
          return ERROR_FAILED_SYSCALL;
        }
      }
    }
  }
  return OK;
}

/* PSEUDO PROTOTYPE FILE MANAGING */

/*
 * name: manager TODO
 * params: arg (not used)
 *
 * Manages reading and sending messages sent by clients.
 * Any unread messages are read, interpreted, and their associated
 * action performed (normally broadcasting it to the other users).
 *
 * Return value: (not used) TODO
 */
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
              // hint that logoff_user will return more than two states later
              STATUS s = logoff_user(listener[i].fd);
              if (s == ERROR_USER_NOT_FOUND) {
                logs("ERR USER NOT FOUND. This should not display. If you see this please post to https://www.github.com/Daves1245/Chime/issues");
              }
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

/*
 * name: main
 * 
 * Setup the server on the requested address,
 * and start the management threads. Gracefully
 * exit when finished.
 */
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

  outputstream = stdout;

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
        logs("Shutting down.");
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

