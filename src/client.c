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
#include <signal.h>
#include <poll.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "signaling.h"
#include "getinet.h"
#include "message.h"
#include "colors.h"
#include "defs.h"
#include "transmitmsg.h"
#include "ftransfer.h"
#include "fileheader.h"
#include "fileinfo.h"
#include "secret.h"
#include "ftrequest.h"

pthread_cond_t file_ready = PTHREAD_COND_INITIALIZER;
pthread_mutex_t finfo_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ft_wait_mutex = PTHREAD_MUTEX_INITIALIZER;

void (*last_action)();
void do_nothing(void) {}
void finally() {
  last_action();
  last_action = do_nothing;
}

status execute_commands(struct connection *, struct message *);

void notify_transfer(void) {
  pthread_cond_broadcast(&file_ready);
  last_action = do_nothing;
}

struct fileinfo finfo;
int running = 1;
long secret;

status handshake(int tsfd, long secret) {
  struct message msg;
  printf("[handshake]: attempting handshake\n");
  memset(&msg, 0, sizeof msg);
  msg.uid = msg.id = 0;
  strcpy(msg.from, " ");
  sprintf(msg.txt, "%ld\n", secret);
  msg.flags = FCONNECT;
  sendmessage(tsfd, &msg);
  recvmessage(tsfd, &msg);
  if (msg.flags == FDISCONNECT) {
    fprintf(stderr, "[handshake]: invalid secret, ft connection refused by server\n");
    return ERROR_CONNECTION_DROPPED;
  } else {
    printf("[handshake]: handshake successful\n");
    return OK;
  }
}

/*
 * TODO the name packmessage should 
 * really refer to the function that
 * packs a message into a character array
 * for sending over a socket.
 *
 * packmessage() - pack a message with the
 * necessary information.
 * @msg: the message to pack
 *
 * Given a message msg, packmessage fills 
 * the text, flags, and timestamp fields
 * within it.
 *
 * Return: OK on success
 */
status packmessage(struct message *msg) {
  msg->flags = FMSG;
  char *txt = fgets(msg->txt, MAX_TEXT_LEN + 1, stdin);
  if (!txt) {
    strcpy(msg->txt, "/exit\n");
  }
  /*
   * TODO ideally you send an empty string
   * but since we currently parse messages
   * with newline delimiters and strtok(),
   * if a packed message contains an empty
   * txt field, then two adjacent newlines
   * form a single delimiter for strtok(),
   * and the last field,flags, is not read
   * properly. So we place a space for now
   * with the intent of changing it later.
   */
  if (*msg->txt == '\n') {
    strcpy(msg->txt, " ");
  }
  timestampmessage(msg);
  return OK;
}

// XXX move src/threading.h into here 
// XXX if (messagetype == FTRANSFER) set fh accordingly
// and
//  pthread_cond_broadcast(&file_ready)
// this should let thread filetransfer upload/download the 

/*
 * sa_handle() - Catch SIGINT and SIGTERM and disconnect from
 * the server
 *
 * The signal handler simply sets the connected
 * flag to false.
 */
void sa_handle(int signal, siginfo_t *info, void *ucontext) {
  connected = 0;
}

/*
 * name: thread_recv
 * params: generic pointer to connection.
 *
 * The main receiving thread for the client.
 * Return value: (unused)
 */
void *thread_recv(void *pconn) {
  struct connection *conn = (struct connection *) pconn;
  struct message msg;
  status s;

  memset(&msg, 0, sizeof msg);
  recvmessage(conn->sfd, &msg);
  secret = atol(msg.txt);

  handshake(conn->transferfd, secret);

  while (connected) {
    s = recvmessage(conn->sfd, &msg);
#ifdef DEBUG
    debugmessage(&msg);
#endif
    if (s == ERROR_CONNECTION_LOST) {
      printf(YELLOW "Connection was closed by the server" ANSI_RESET "\n");
      // XXX give a 'server closed connection' return state to thread
      return NULL;
    }
    switch (msg.flags) {
      case FDISCONNECT:
        printf(YELLOW "[%s left the chat]" ANSI_RESET "\n", msg.from);
        break;
      case FCONNECT:
        printf(YELLOW "[%s entered the chat]" ANSI_RESET "\n", msg.from);
        break;
      case FMSG:
        showmessage(&msg);
        break;
      case ECONNDROPPED:
        printf(YELLOW "Connection was closed by the server" ANSI_RESET "\n");
        break;
      default:
        printf(RED "[invalid flags, defaulting to displaymsg]" ANSI_RESET "\n");
        showmessage(&msg);
        break;
    }
  }
  return NULL;
}

/*
 * name: thread_send
 * params: generic pointer to connection
 *
 * The main send thread for clients. Handles
 * packing a message with user and text info,
 * and then sends the packed message to the server
 */
void *thread_send(void *pconn) {
  struct connection *conn = (struct connection *) pconn;
  struct message msg;
  struct pollfd listener;

  // pack msg with user info and send to server
  memset(&msg, 0, sizeof msg);
  makemessage(&conn->uinfo, &msg);
  strcpy(msg.txt, " ");
  msg.flags = FCONNECT;
  sendmessage(conn->sfd, &msg);
  msg.flags = FMSG;

  listener.fd = 0; // poll for stdin
  listener.events = POLLIN; // wait till we have input

  while (connected) {
    if (poll(&listener, 1, POLL_TIMEOUT) && listener.revents == POLLIN) {
      /* XXX Grab input, check for exit */
      packmessage(&msg);
      execute_commands(conn, &msg);
      msg.id++;
      status s = sendmessage(conn->sfd, &msg);
      if (s != OK || msg.flags == FDISCONNECT) {
        printf("sendmessage returned non-OK or DISCONNECT status. Exiting...\n");
        connected = 0;
      }
      finally();
    }
  }
  disconnect_wrapper_and_exit(conn->sfd);
  return NULL;
}

status request_transfer(struct ftrequest *request) {
  pthread_t id;
  printf("inside request_transfer conn is %p\n", request->conn);
  if (!request) {
#ifdef DEBUG
    printf("request_transfer called with NULL argument!\n");
#endif
    return ERROR_INVALID_ARGUMENTS;
  }
  if (pthread_create(&id, NULL, filetransfer, request)) {
    perror("pthread_create");
    return ERROR_FAILED_SYSCALL;
  }
#ifdef DEBUG
  printf("[request_transfer]: pthread created with id %ld\n", id);
#endif
  if (pthread_detach(id)) {
    printf("[request_transfer]: thread created for file transfer is not joinable\n");
  }
  free(request);
  return OK;
}

// necessary file and then sleep again

/*
 * name: cmdparse
 * params: message pointer msg
 *
 * Performs the necessary action that
 * the command in msg specifies
 */
status execute_commands(struct connection *conn, struct message *msg) {
  char *tmp, buff[MAX_TEXT_LEN + 1];

  msg->flags = FMSG;
  if (*msg->txt != '/') {
    printf("not a command!\n");
    return OK; // not a command
  }
  strcpy(buff, msg->txt + 1);
  tmp = strtok(buff, " ");
  /* Commands with no arguments */
  if (!tmp) {
    if (strcmp(buff, "exit\n") == 0) {
      printf("DISCONNECTING\n");
      msg->flags = FDISCONNECT;
      return OK;
    }
    /* Commands with arguments */
  } else {
    if (strcmp(buff, "upload") == 0) {
      char *filename;

      filename = strtok(NULL, " ");
      filename[strcspn(filename, "\r\n")] = '\0';

      if (filename) {
        struct ftrequest *upload_request;
        if (create_upload_request(&upload_request, filename) != OK) {
          fprintf(stderr, "Could not create upload request\n");
          return OK;
        }
        printf("CONN IS %p\n", conn);
        upload_request->conn = conn;
        msg->flags = FTRANSFER;
        if (request_transfer(upload_request) != OK) {
          fprintf(stderr, "Upload request returned non-OK status\n");
        }
      } else {
        printf("usage: /upload [filename]\n");
      }
    }
  }
  return OK;
}

// XXX status login(struct connection *conn) {}

/*
 * name: main
 *
 * Connect to the server and talk. Disconnect
 * gracefully on exit.
 */
int main(int argc, char **argv) {
  int sockfd, transferfd;
  char *port, *ftport;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  char serverip[INET6_ADDRSTRLEN];
  char *hostname = LOCALHOST;
  struct sigaction s_act, s_oldact;
  int res;
  pthread_t sendertid;
  pthread_t receivertid;
  struct connection conn;

  port = PORT;
  ftport = FTPORT;

  if (argc > 1) {
    hostname = argv[1];
  }

  if (argc > 2) {
    port = argv[2];
  }

  if (argc > 3) {
    ftport = argv[3];
  }

  last_action = do_nothing;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if ((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  printf("Attempting to connect...\n");
  // loop through all the results and connect to the first we can
  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("socket");
      continue;
    }
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("connect");
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

  /* Connect for file transfer */
  if ((rv = getaddrinfo(hostname, ftport, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    exit(EXIT_FAILURE);
  }
  for (p = servinfo; p; p = p->ai_next) {
    if ((transferfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("socket");
      continue;
    }
    if (connect(transferfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(transferfd);
      perror("connect");
    }
    break;
  }

  if (!p) {
    fprintf(stderr, "Could not connect to file transfer port.\n");
  }
  conn.transferfd = transferfd;
  freeaddrinfo(servinfo);

  /* Implement signal handling */
  s_act.sa_sigaction = sa_handle;
  s_act.sa_flags = SA_SIGINFO;
  res = sigaction(SIGTERM, &s_act, &s_oldact);

  if (res != 0) {
    perror("sigaction");
  }
  res = sigaction(SIGINT, &s_act, &s_oldact);
  if (res != 0) {
    perror("sigaction");
  }

  conn.sfd = sockfd;
  //login(&conn);

  /* Tell the server who we are */
  printf("handle:");
  fgets(conn.uinfo.handle, HANDLE_LEN + 1, stdin);

  if (pthread_create(&sendertid, NULL, thread_send, &conn)) {
    fprintf(stderr, "Could not create msg sender thread\n");
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }

  if (pthread_create(&receivertid, NULL, thread_recv, &conn)) {
    fprintf(stderr, "Could not create msg receiving thread\n");
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }

  /* Wait for threads to terminate and exit */
  pthread_join(sendertid, NULL);
  pthread_join(receivertid, NULL);

  close(sockfd);
  return 0;
}

