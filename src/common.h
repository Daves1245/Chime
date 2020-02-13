#include <inttypes.h>
#include <time.h>
#include <signal.h>

#include "colors.h"

// Arbitrary
#define MAX_RECV_LEN 100
#define MAX_SEND_LEN 100

#define MAX_PACKET_PAYLOAD_SIZE 1400
#define MAX_PACKETS_PER_MSG 256 // handle input up to 358400 bytes long

#define MAX_NAME_LEN 30
#define MAX_TEXT_LEN 3000

#define HASH_LEN 256 // length of hash

#define UINT64_BASE10_LEN 21 // XXX rename this atrocity
#define UINT32_BASE10_LEN 12 // XXX number of base10 digits needed to store all values of 32 bit integer

#define FMSG 0x0
#define FCONNECT 0x1
#define FDISCONNECT 0x2

pthread_t managerid;
pthread_t senderid;
pthread_t receiverid;

int sfd;

struct handlerinfo {
  int sfd;
  char *handle;
};

struct message {
  uint64_t id;
  char from[MAX_NAME_LEN + 1];
  char text[MAX_TEXT_LEN + 1];
  uint32_t flags;
};

struct user {
  uint16_t userid;
  char handle[MAX_NAME_LEN + 1];
  // XXX add friends
};

void endconnection();                               /* Terminate the connection */
void getinput(char *dest, size_t *res, size_t len); /* store '\n'-terminated line at most len into dest and modify *res accordingly */
void *thread_recv(void *);                          /* Message receiving thread */
void *thread_send(void *);                          /* Message sending thread */
void *connection_handler(void *);                   /* Connection handling thread */
void hashmsg(struct message *msg, char *res);       /* Hash a message and store it in res */
int sendmessage(int sfd, struct message *msg);      /* Send a message to sfd */
int recvmessage(int sfd, struct message *msg);      /* Get a message from sfd */
// XXX - implement time stamping feature
void timestamp(struct message *m);                  /* Timestamp a message */

/* Wrapper for send function; XXX - add failsafes */
int trysend(int fd, void *buff, size_t bufflen) {
  size_t sent;
  if ((sent = send(fd, buff, bufflen, 0)) != bufflen) {
    if (sent < 0) {
      perror("send");
      return errno;
    } else {
      perror("incomplete send");
      return errno;
    }
  }
  return 0;
}

/* Wrapper for recv function; XXX - add failsafes */
int tryrecv(int fd, void *buff, size_t bufflen) {
  size_t recvd;
  if ((recvd = recv(fd, buff, bufflen, 0)) != bufflen) {
    if (recvd < 0) {
      perror("recv");
      return errno;
    } else if (recvd == 0) {
      printf(RED "Connection has closed, cannot send" ANSI_RESET "\n");
      return errno;
    }
  }
  return 0;
}

// XXX
void hashmsg(struct message *msg, char *res) {
  res = '\0';
  return;
}

/*
 * purpose: send type of data being sent first
 * "MSG", "REQ", "ACK", "STP", etc.
 * for message, request, ack, stamp, respectively.
 * make this for each message-encapsulating type
 */
void initsendmsg(int sfd) {
  trysend(sfd, "MSG:", sizeof "MSG:");
}

/* Unpack struct, send each field as char stream */
int sendmessage(int sfd, struct message *msg) {
  // initsendmsg(sfd);


  char hash[HASH_LEN];
  char id[UINT64_BASE10_LEN + 1]; // +1 for ':' delimiter
  char flags[UINT32_BASE10_LEN + 1];

  hashmsg(msg, hash);

  sprintf(id, "%012" PRIu64, msg->id);
  sprintf(flags, "%012" PRIu32, msg->flags);

  id[UINT64_BASE10_LEN] = ':';
  flags[UINT32_BASE10_LEN] = ':';

#ifdef DEBUG
  printf("[DEBUG]: id `%s`\n", id);
  printf("[DEBUG]: flags `%s`\n", flags);
  printf("[DEBUG]: from `%s`\n", msg->from);
  printf("[DEBUG]: text `%s`\n", msg->text);
#endif

  trysend(sfd, id, sizeof id);
  trysend(sfd, msg->from, MAX_NAME_LEN + 1);
  trysend(sfd, msg->text, MAX_TEXT_LEN + 1);
  trysend(sfd, flags, sizeof flags);
  return 0; // XXX not finished
}

/*
 * we have just received "MSG:"
 * over the wire. Now, we put the incoming message
 * into the msg pointer
 * "
 */
int recvmessage(int sfd, struct message *msg) {
  char id[UINT64_BASE10_LEN + 1]; // +1 for ':' delimiter
  char from[MAX_NAME_LEN + 1]; // +1 for ':' delimiter
  char text[MAX_TEXT_LEN + 1]; // +1 for ':' delimiter
  char flags[UINT32_BASE10_LEN + 1]; // +1 for ':' delimiter

  tryrecv(sfd, id, sizeof id);
  tryrecv(sfd, from, sizeof from);
  tryrecv(sfd, text, sizeof text);
  tryrecv(sfd, flags, sizeof flags);

  msg->id = atoll(id);
  memcpy(msg->from, from, MAX_NAME_LEN);
  msg->from[MAX_NAME_LEN] = '\0';
  memcpy(msg->text, text, MAX_TEXT_LEN);
  msg->text[MAX_TEXT_LEN] = '\0';
  msg->flags = atoll(flags);

#ifdef DEBUG
  printf("[RECEIVED] id %" PRIu64 "\n", msg->id);
  printf("[RECEIVED] from %s\n", msg->from);
  printf("[RECEIVED] text %s\n", msg->text);
  printf("[RECEIVED] flags %" PRIu32 "\n", msg->flags);
#endif

  return 0; // XXX not finished
}

/*
 * XXX - implement actual end connection protocol
 * - check for recognition of logging off
 */
void endconnection(void) {
  struct message tmp;
  memset(&tmp, 0, sizeof tmp);
  tmp.flags = FDISCONNECT;
  printf("test1\n");
  trysend(sfd, &tmp, sizeof tmp);
  printf("test2\n");
  close(sfd);
  printf("Disconnected\n");
  exit(EXIT_SUCCESS);
}

// get sockaddr, IPv4 or IPv6
void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

void getinput(char *dest, size_t *res, size_t len) {
  int c;
  for (int i = 0; i < len; i++) {
    c = getchar();
    if (c == '\n' || c == EOF) {
      *res = i + 1;
      break;
    }
    dest[i] = c;
  }
  dest[*res - 1] = '\0';
}

void *thread_recv_old(void *sfdp) {
  int sfd = *((int *)sfdp);
  char buff[MAX_RECV_LEN];
  while (1) {
    if (recv(sfd, buff, MAX_RECV_LEN, 0) == -1) {
      perror("recv");
      break;
    }
    if (strcmp(buff, "exit") == 0) {
      endconnection();
      break;
    }
    printf("\n%s\n", buff);
  }
  return NULL; // XXX pthread_exit(retvalue);
}

void *thread_recv(void *handlei) {
  struct handlerinfo *info = handlei;
  struct message msg;
  while (1) {
    recvmessage(info->sfd, &msg);
    switch (msg.flags) {
      case FDISCONNECT:
        printf(YELLOW "[%s left the chat]" ANSI_RESET "\n", msg.from);
        break;
      case FCONNECT:
        printf(YELLOW "[%s entered the chat]" ANSI_RESET "\n", msg.from);
        break;
      case FMSG:
        printf(CYAN "[%s]" ANSI_RESET ": %s\n", msg.from, msg.text);
        break;
    }
  }
}

void *thread_send(void *handlei) {
  struct handlerinfo *info = handlei;
  struct message msg;
  size_t msgtextlen;

  memset(&msg, 0, sizeof msg);
  memcpy(msg.from, "HELLO\0", 6);
  strcpy(msg.from, info->handle);

  struct pollfd listener;
  listener.fd = 0; // poll for stdin
  listener.events = POLLIN; // wait till we have input

  while (1) {
    poll(&listener, 1, -1); // block until we can read
    if (listener.revents == POLLIN) {
      /* XXX Grab input, check for exit */
      getinput(msg.text, &msgtextlen, MAX_TEXT_LEN);
      msg.id++;
      sendmessage(info->sfd, &msg);
      if (strcmp(msg.text, "/exit") == 0) {
        exit(EXIT_SUCCESS);
      }
    }
  }
}

/* deprecated */
void *thread_send_old(void *sfdp) {
  int sfd = *((int *)sfdp);
  char buff[MAX_SEND_LEN];
  size_t msglen;
  int s;

  struct pollfd listener;
  listener.fd = 0; // poll for stdin
  listener.events = POLLIN; // wait till we have input

  while (1) {
    poll(&listener, 1, -1); // block until we can read
    if (listener.revents == POLLIN) { 
      /* Grab input, check for exit */
      getinput(buff, &msglen, MAX_SEND_LEN);
      if (strcmp(buff, "exit") == 0) {
        printf("exiting.\n");
        endconnection();
        break;
      }

      /* Send the message */
      if ((s = send(sfd, buff, msglen, 0)) != msglen) {
        if (s < 0) {
          perror("send");
          break;
        } else {
          perror("incomplete send");
          break;
        }
      }
    }
  }
  return NULL; // XXX thread_exit(retvalue);
}

void *connection_handler(void *arg) {
  managerid = pthread_self();

  if (pthread_create(&senderid, NULL, thread_send, arg)) {
    fprintf(stderr, "Could not create message sending thread\n");
    perror("pthread_create");
    return NULL; // XXX pthread_exit(retvalue)
  }
  if (pthread_create(&receiverid, NULL, thread_recv, arg)) {
    fprintf(stderr, "Could not create message receiving thread\n");
    perror("pthread_create");
    // XXX kill sender thread 
    return NULL; // XXX pthread_exit(retvalue);
  }

  // XXX failure should kill all threads
  pthread_join(senderid, NULL);
  pthread_join(receiverid, NULL);

  return NULL; // XXX pthread_exit(retvalue);
}
