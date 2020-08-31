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
#include <sys/stat.h>

#include "colors.h"
#include "getinet.h"
#include "message.h"
#include "defs.h"
#include "connection.h"
#include "transmitmsg.h"
#include "signaling.h"
#include "logging.h"
#include "fileheader.h"
#include "ftransfer.h"
#include "math.h"
#include "fileinfo.h"
#include "secret.h"
#include "ftrequest.h"

#define PORT "33401"
#define BACKLOG 10
#define MODE 0666 // TODO mv to defs

#define UPTIME_STR_LEN 10
#define LOGBUFF_STR_LEN 100

/* Each file sent is prepended by this so that the recipient knows what to expect */
struct p2p_request {
  int requester_uid; /* user requesting to connect */
  int requestee_uid; /* user being connected to */
};

struct p2p_info {
  char server_host[INET6_ADDRSTRLEN], server_port[10];
  char client_host[INET6_ADDRSTRLEN], client_port[10];
};

FILE *outputstream;
int upload_dir_fd = -1;

int transfer_conns_arr[BACKLOG];
int *transfer_conns;
int num_transfer_conns;

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
  for (iterator = connections; iterator && iterator->next != connections; iterator = iterator->next) {
    disconnect(iterator);
  }
  if (iterator) {
    disconnect(iterator);
  }
}

/*
 * name: login_user 
 * params: urequest
 *
 * Check if provided user hints are valid
 * and if so, login the new user.
 */
status login_user(struct connection *entry) {
  struct connection *iterator;
  if (!connections) {
    connections = entry;
    connections->next = connections->prev = entry;
    entry->uinfo.uid = next_uid++;
    printf("hm\n");
    return OK;
  }
  printf("beginning search for duplicate username\n");
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
status logoff_user(int sfd) {
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

status setup_p2p(struct p2p_request *req) {
  return OK;
}

void *on_finish_do(void *arg) {
  return NULL; 
}

void *file_transfer(void *ftreq) {
  struct ftrequest *req = (struct ftrequest *) ftreq;
  struct fileheader header;
  struct message response;

  memset(&response, 0, sizeof response);
  response.uid = response.id = 0;
  timestampmessage(&response);
  strcpy(response.from, "server");
  response.flags = FMSG;

  logs("[file_transfer]: started new thread. Received argument:");
  printf("sfd: %d\ntransferfd:%d\nhandle:%s\n", req->conn->sfd, req->conn->transferfd, req->conn->uinfo.handle);

  if (!req) {
    logs("[file_transfer]: called with null argument when non-null required. Aborting...");
    return NULL;
  } 

  switch (req->finfo.status) {
    case UPLOAD:
      if (recvheader(req->conn->transferfd, &header) != OK) {
        logs(CHIME_WARN "[file_transfer]: recvheader returned non-OK status");
        /* Handle non-fatal errors */
      }
      if (strcmp(req->finfo.header.filename, header.filename) != 0 || header.size != req->finfo.header.size) {
        logs(CHIME_WARN "[file_transfer]: WARNING: received header from client differs from local header!");
        /* TODO print header info */
        strcpy(response.txt, "NO"); // be forceful
        sendmessage(req->conn->transferfd, &response);
      } else {
        strcpy(response.txt, "OK");
        sendmessage(req->conn->transferfd, &response);
      }
      if (uploadfile(req->conn->transferfd, req->finfo.fd, &req->finfo.header) != OK) {
        logs(CHIME_WARN "[file_transfer]: uploadfile returned non-OK status");
        /* Handle non-fatal errors */
      }
      logs(CHIME_INFO "[file_transfer]: finished upload procedure");
      break;
    case DOWNLOAD:
      logs(CHIME_INFO "[file_transfer]: Sending file header...");
      if (sendheader(req->conn->transferfd, &req->finfo.header) != OK) {
        logs(CHIME_WARN "[file_transfer]: recvheader returned non-OK status");
        /* Handle non-fatal errors */
      }
      logs(CHIME_INFO "[file_transfer]: Done.");
      logs(CHIME_INFO "[file_transfer]: Downloading file...");
      if (downloadfile(req->conn->transferfd, req->finfo.fd, &req->finfo.header) != OK) {
        logs(CHIME_WARN "[file_transfer]: downloadfile returned non-OK status");
      }
      strcpy(response.txt, "OK");
      sendmessage(req->conn->transferfd, &response);
      logs(CHIME_INFO "[file_transfer]: finished file download procedure");
      break;
    case NOT_READY:
      logs(CHIME_WARN "[file_transfer]: file_transfer called with NOT_READY file status. Exiting");
    default:
      logs(CHIME_WARN "[file_transfer]: file_transfer called with invalid transfer status");
      break;
  }
  close(req->finfo.fd);
  logs(CHIME_INFO "[file_transfer]: file_transfer thread exiting");
  return NULL;
}

/* PROTOTYPE FILE MANAGEMENT */
void *transfermanager(void *arg) {
  int listenfd, newfd, rv;
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_storage their_addr;
  socklen_t sin_size;
  int yes = 1;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if ((rv = getaddrinfo(NULL, "33402", &hints, &servinfo)) != 0) {
    logs(CHIME_WARN "Could not set up file transfering thread\n");
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return NULL; // TODO proper return value
  }

  for (p = servinfo; p; p = p->ai_next) {
    if ((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("socket");
      continue;
    }

    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      logs(CHIME_WARN "encountered an error while setting up file socket");
      logs(CHIME_WARN "the server may be unable to bind to the port");
      perror("setsockopt");
    }

    if (bind(listenfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(listenfd);
      perror("bind");
      continue;
    }
    break;
  }

  if (!p) {
    logs(CHIME_WARN "Unable to connect to second port. Setting client to transfer over msg instead");
    logs(CHIME_WARN "Warning: Latency proportional to file size may be experienced");
    // logs(CHIME_WARN "Set option NO_TRANSFER_ON_MSG to disable this feature");
    // XXX do above
  }

  if (listen(listenfd, BACKLOG) == -1) {
    logs(CHIME_WARN "Could not set up socket for listening");
    perror("listen");
    return NULL;
  }

  logs(CHIME_INFO "File transfer setup successful");

  while (running) {
    struct message request, response;
    long received_secret;
    struct connection *iterator;
    int flag = 0, found = 0;

    newfd = accept(listenfd, (struct sockaddr *) &their_addr, &sin_size);
    if (newfd < 0) {
      if (errno != EINTR) {
        logs(CHIME_WARN "Warning: Error occured while listening for file transfer connections");
      }
      break;
    }

    /* Authenticate with currently connected char-message user */
    // TODO server sends WHO request first
    memset(&request, 0, sizeof request);

    memset(&response, 0, sizeof response);
    request.uid = response.uid = 0;
    strcpy(response.from, "SERVER"); // TODO make MESSAGE_INIT
    recvmessage(newfd, &request);
    received_secret = atol(request.txt);
    printf("received secret: %ld\n", received_secret);
    /* XXX This is bad, and needs changing ASAP */
    for (iterator = connections; !flag || iterator != connections; iterator = iterator->next) {
      flag |= iterator == connections;
      if (iterator->secret == received_secret) {
        printf(CHIME_INFO "[transfermanager]: Found client with secret %ld's parent connection, %p and setting transferfd to %d\n", received_secret, iterator, newfd);
        iterator->transferfd = newfd; // this is so bad
        found = 1;
      }
    }

    if (!found) {
      strcpy(response.txt, "INVALID SECRET\n");
      response.flags = FDISCONNECT;
      sendmessage(newfd, &response);
      close(newfd);
      continue;
    }

    strcpy(response.txt, "OK");
    response.flags = FCONNECT;
    sendmessage(newfd, &response);

    transfer_conns[num_transfer_conns] = newfd;
    num_transfer_conns++;
  }
  return NULL;
}

struct connection *find_conn_by_sfd(int sfd) {
  struct connection *iterator;
  int flag = 0;
  for (iterator = connections; !flag || iterator != connections; iterator = iterator->next) {
    flag |= iterator == connections;
    if (iterator->sfd == sfd) {
      return iterator;
    }
  }
  return NULL;
}

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
          if (m.flags == FTRANSFER) {
            pthread_t ftransferinstance; 
            struct message response;
            struct ftrequest request;

            logs(CHIME_INFO "Received upload request\n");
            memset(&response, 0, sizeof response);
            memset(&request, 0, sizeof request);

            request.finfo.status = DOWNLOAD;
            /* TODO make this O(1) */
            request.conn = find_conn_by_sfd(listener[i].fd);
            if (!request.conn) {
              logs(CHIME_WARN "[manager]: Could not find connection by sfd for file transfer. Aborting...");
              continue; /* TODO Don't do this! */
            }

            if (recvheader(listener[i].fd, &request.finfo.header) != OK) {
              /* Handle non-fatal errors */
              logs(CHIME_WARN "recvheader after FTRANSFER request returned non-OK status");
            }

            /* TODO fix impending duplicate-name bug */
            while ((request.finfo.fd = openat(upload_dir_fd, request.finfo.header.filename, O_CREAT | O_WRONLY, MODE)) == -1 && errno == EINTR);
            if (request.finfo.fd < 0) {
              logs(CHIME_WARN "Could not create a file for file transfer");
              perror("write");
              return NULL;
            }

            if (pthread_create(&ftransferinstance, NULL, file_transfer, &request)) {
              logs(CHIME_WARN "could not create thread for handling file transfer");
              perror("pthread_create");
            } else {
              logs(CHIME_INFO "File transfer thread created successfully");
            }
            continue;
          }
          /* Connect a new user */
          if (m.flags == FCONNECT) {
            struct message ret;
            struct connection *conn;
            long secret;

            conn = malloc(sizeof(struct connection));
            if (!conn) {
              fprintf(stderr, "Could not allocate memory for user connection\n");
              /* XXX disconnect all users here */
              _Exit(EXIT_FAILURE);
            }

            memset(conn, 0, sizeof(*conn));
            memset(&ret, 0, sizeof ret);
            secret = gen_secret();
            printf("sending client a secret of %ld\n", secret);
            sprintf(ret.txt, "%ld\n", secret);
            strcpy(ret.from, "SERVER");
            timestampmessage(&ret);
            sendmessage(listener[i].fd, &ret);

            conn->sfd = listener[i].fd;
            conn->next = conn->prev = conn; // TODO make CONNECTION_INIT(conn);
            conn->secret = secret;
            strcpy(conn->uinfo.handle, m.from);
            status s = login_user(conn);
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
              status s = logoff_user(listener[i].fd);
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

void init(void) {
  srand(time(NULL));
  transfer_conns = transfer_conns_arr;

  mkdir("uploads", MODE);
  upload_dir_fd = open("uploads", O_DIRECTORY);
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
  pthread_t managert;
  pthread_t transfert;

  if (argc >= 2) {
    port = argv[2];
  }

  log_init();
  init();

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

  if (pthread_create(&managert, NULL, manager, NULL)) {
    logs(CHIME_FATAL "FATAL: Unable to create manager thread");
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }

  if (pthread_create(&transfert, NULL, transfermanager, NULL)) {
    logs(CHIME_WARN "WARNING: Unable to create file transfering thread");
    perror("pthread_create");
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

  /* Wait for managing thread to finish */
  pthread_join(managert, NULL);
  /* Disconnect and exit cleanly */
  logs("Cancelling threads...");
  pthread_cancel(transfert);
  logs("Disconnecting all users...");
  freeconnections(connections);
  logs("Done.");
  exit(EXIT_SUCCESS);
}

