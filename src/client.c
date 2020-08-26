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
#include "common.h"
#include "message.h"
#include "colors.h"
#include "defs.h"
#include "threading.h"
#include "transmitmsg.h"
#include "transmitfile.h"
#include "fileheader.h"
#include "fileinfo.h"
#include "secret.h"

#define LOCALHOST "127.0.0.1"
#define PORT "33401"
#define MAXDATASIZE 100

pthread_cond_t file_ready = PTHREAD_COND_INITIALIZER;
pthread_mutex_t filei_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t received_secret = PTHREAD_COND_INITIALIZER;
pthread_mutex_t secret_mutex = PTHREAD_MUTEX_INITIALIZER;

void (*finally)();
void do_nothing(void) {}

void notify_transfer(void) {
  pthread_cond_broadcast(&file_ready);
  finally = do_nothing;
}

struct fileinfo finfo;
int running;
long secret;

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
STATUS packmessage(struct message *msg) {
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
  if (*msg->txt == '/') {
    cmdparse(msg);
  }
  timestampmessage(msg);
  return OK;
}

// XXX move src/threading.h into here 
// XXX if (messagetype == FUPLOAD) set fh accordingly
// and
//  pthread_cond_broadcast(&file_ready)
// this should let thread filetransfer upload/download the 
// necessary file and then sleep again

/*
 * name: cmdparse
 * params: message pointer msg
 *
 * Performs the necessary action that
 * the command in msg specifies
 */
STATUS cmdparse(struct message *msg) {
  if (*msg->txt != '/') {
    return OK; // not a command
  }
  char buff[MAX_TEXT_LEN + 1];
  strcpy(buff, msg->txt + 1);
  strtok(buff, " ");
  if (strcmp(buff, "upload") == 0) {
    char *filename;

    filename = strtok(NULL, " ");
    filename[strcspn(filename, "\r\n")] = '\0';

    printf("FILENAME: `%s`\n", filename);
    if (filename) {
      struct stat st;
      int fd;

      msg->flags = FUPLOAD;
      while ((fd = open(filename, O_RDONLY)) == -1 && errno == EINTR);
      if (fd < 0 ) {
        fprintf(stderr, RED "File path must be valid\n" ANSI_RESET);
        perror("open");
        return ERROR_INVALID_FILEPATH;
      }
      if (stat(filename, &st) != 0) {
        fprintf(stderr, "Could not stat file\n");
        perror("stat");
        exit(EXIT_FAILURE); // XXX fatal?
      }

      /* This shouldn't be necessary so far since one
       * thread stricly reads and the other strictly writes
       * but it's good practice and allows for later changes to
       * be made easily */
      pthread_mutex_lock(&filei_mutex);
      finfo.fd = fd;
      finfo.header.size = st.st_size;
      strcpy(finfo.header.filename, filename); // TODO only pass basename
      pthread_mutex_unlock(&filei_mutex);
    } else {
      printf("usage: /upload [filename]\n");
    }
  }
  return OK;
}


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
  STATUS s;

  memset(&msg, 0, sizeof msg);
  recvmessage(conn->sfd, &msg);
  debugmessage(&msg);
  secret = atol(msg.txt);
  printf("received secret %ld\n", secret);

  pthread_cond_broadcast(&received_secret);

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

  // pack msg with user info and send to server
  memset(&msg, 0, sizeof msg);
  makemessage(&conn->uinfo, &msg);
  strcpy(msg.txt, " "); // TODO fix the 'empty text field' bug
  msg.flags = FCONNECT;
  sendmessage(conn->sfd, &msg);
  msg.flags = FMSG;

  struct pollfd listener;
  listener.fd = 0; // poll for stdin
  listener.events = POLLIN; // wait till we have input

  while (connected) {
    if (poll(&listener, 1, POLL_TIMEOUT) && listener.revents == POLLIN) {
      /* XXX Grab input, check for exit */
      packmessage(&msg);
      msg.id++;
      if (msg.flags == FUPLOAD) {
        finally = notify_transfer;
      }
      STATUS s = sendmessage(conn->sfd, &msg);
      if (s != OK || msg.flags == FDISCONNECT) {
        connected = 0;
      }
      finally();
    }
  }
  disconnect_wrapper_and_exit(conn->sfd);
  return NULL;
}

void *filetransfer(void *pconn) {
  struct connection *conn = (struct connection *) pconn;
  int sockfd;
  char *hostname = "127.0.0.1"; // XXX
  char *port = "33402";
  struct addrinfo hints, *servinfo, *p;
  int rv;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if ((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return NULL;
  } 
  for (p = servinfo; p; p = p->ai_next) {
    printf("trying\n");
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("socket");
      continue;
    }
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("connect:");
    }
    break;
  }

  if (!p) {
    fprintf(stderr, "Could not connect to transfer port\n");
    return NULL;
  }

  /* Not entirely sure how to use pthread conditional wait.
   * I decided to use this over while (!secret) {} because the
   * latter would waste cpu cycles. Hopefully I'm using this
   * correctly */
  pthread_mutex_lock(&secret_mutex);
  while (!secret) {
    pthread_cond_wait(&received_secret, &secret_mutex);
  }
  pthread_mutex_unlock(&secret_mutex);

  struct message request;
  memset(&request, 0, sizeof request);
  request.uid = request.id = 0;
  strcpy(request.from, " ");
  sprintf(request.txt, "%ld\n", secret);
  request.flags = FCONNECT;
  sendmessage(sockfd, &request);
  recvmessage(sockfd, &request);
  if (request.flags == FDISCONNECT) {
    fprintf(stderr, "Could not connect to second port: Secret was invalid\n");
    return NULL;
  } else {
    printf("Successfully connected to second port\n");
  }


  /* Sleep until signaled to upload or download a file, then set a status and
   * sleep again */
  while (running) {
    pthread_mutex_lock(&filei_mutex);
    while (finfo.status == NOT_READY) {
      pthread_cond_wait(&file_ready, &filei_mutex);
    }

    STATUS s1, s2;
    switch (finfo.status) {
      case UPLOAD:
        s1 = sendheader(conn->transferfd, &finfo.header);
        if (s1 != OK) {
          printf("SENDHEADER RETURNED NON-OK %d STATUS\n", s1);
        }
        s2 = uploadfile(conn->transferfd, finfo.fd, &finfo.header);
        if (s2 != OK) {
          /* Handle non-fatal errors */
          printf("UPLOADFILE RETURNED NON-OK %d STATUS\n", s2);
        }
        break;
      case DOWNLOAD:
        s1 = recvheader(conn->transferfd, &finfo.header);
        if (s1 != OK) {
          /* Handle non-fatal errors */
        }
        s2 = downloadfile(conn->transferfd, finfo.fd, &finfo.header);
        break;
      case NOT_READY:
        fprintf(stderr, "File thread awoken when file not ready to be handled\n");
        return NULL; // TODO fatal?
        break;
      default:
        /* fprintf(stderr, "[filemanager]: Unrecognized option") */
        break;
    }
    finfo.status = NOT_READY;
    pthread_mutex_unlock(&filei_mutex);
  }
  return NULL;
}

// XXX status login(struct connection *conn) {}

/*
 * name: main
 *
 * Connect to the server and talk. Disconnect
 * gracefully on exit.
 */
int main(int argc, char **argv) {
  int sockfd;
  char *port;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  char serverip[INET6_ADDRSTRLEN];
  char *hostname = LOCALHOST;
  struct sigaction s_act, s_oldact;
  int res;
  pthread_t sendertid;
  pthread_t receivertid;
  pthread_t filetransferid;
  struct connection conn;

  port = PORT;

  if (argc > 1) {
    hostname = argv[1];
  }

  if (argc > 2) {
    port = argv[2];
  }

  finally = do_nothing;

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

  if (pthread_create(&filetransferid, NULL, filetransfer, &conn)) {
    fprintf(stderr, "Could not create filetransfer thread\n");
    perror("pthread_crete");
    exit(EXIT_FAILURE);
  }

  /* Wait for threads to terminate and exit */
  pthread_join(sendertid, NULL);
  pthread_join(receivertid, NULL);

  close(sockfd);
  return 0;
}

