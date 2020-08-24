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

#include "signaling.h"
#include "common.h"
#include "message.h"
#include "colors.h"
#include "defs.h"
#include "threading.h"
#include "transmitmsg.h"

#define LOCALHOST "127.0.0.1"
#define PORT "33401"
#define MAXDATASIZE 100

pthread_cond_t file_ready = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// XXX move fh from server to its own file and include it here
struct fileheader {
  char *filename;
  int size;
};

/* Should be modified only when ready to send */
struct fileheader fh;
// XXX move src/threading.h into here 
// XXX if (messagetype == FUPLOAD) set fh accordingly
// and
//  pthread_cond_broadcast(&file_ready)
// this should let thread filetransfer upload/download the 
// necessary file and then sleep again

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

void *filetransfer(void *arg) {
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
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("socket");
      continue;
    }
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("connect:");
    }
  }

  if (!p) {
    fprintf(stderr, "Could not connect to transfer port\n");
    return NULL;
  }

  /* Sleep until activated to upload or download a file, then set a status and
   * sleep again */
  int condition = 0;
  pthread_mutex_lock(&mutex);
  while (!condition) {
    pthread_cond_wait(&file_ready, &mutex);
  }
  pthread_mutex_unlock(&mutex);
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
  struct connection conn;

  port = PORT;

  if (argc > 1) {
      hostname = argv[1];
  }

  if (argc > 2) {
    port = argv[2];
  }

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

  /* Wait for threads to terminate and exit */
  pthread_join(sendertid, NULL);
  pthread_join(receivertid, NULL);

  close(sockfd);
  return 0;
}

