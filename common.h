// Arbitrary
#define MAX_RECV_LEN 100
#define MAX_SEND_LEN 100

#define MAX_PACKET_PAYLOAD_SIZE 1400
#define MAX_PACKETS_PER_MSG 256 // handle input up to 358400 bytes long

#define MAX_NAME_LEN 30
#define MAX_TEXT_LEN 3000

#define HASH_LEN 256 // length of hash

#define UNIT64_BASE10_LEN 21 // XXX rename this atrocity
#define UINT32_BASE10_Len 12 // XXX number of base10 digits needed to store all values of 32 bit integer

struct handlerinfo {
  int sfd;
};

struct packet {
  unsigned short id;
  unsigned char numpacks;
  unsigned char index;
  unsigned char flags;
  char text[MAX_PACKET_PAYLOAD_SIZE]; // 1500 ethernet limit 
};

struct message {
  uint64_t id;
  char from[NAME_LEN];
  char text[MAX_TEXT_LEN];
  uint32_t flags;
};

struct msg_hash {
  uint64_t id; 
  char hash[HASH_LEN];
}

void endconnection(int fd);                         /* Terminate the connection */
void getinput(char *dest, size_t *res, size_t len); /* store '\n'-terminated line at most len into dest and modify *res accordingly */
void *thread_recv(void *);                          /* Message receiving thread */
void *thread_send(void *);                          /* Message sending thread */
void *connection_handler(void *);                   /* Connection handling thread */
int recvpacket(struct packet *m, int sfd);          /* Send a message through sfd */
int sendpacket(struct packet *m, int sfd);          /* Receive a message from sfd */

/* Wrapper for send function; XXX - add failsafes */
int trysend(int fd, void *buff, size_t bufflen) {
  size_t send;
  if ((sent = send(fd, buff, bufflen, 0)) != bufflen) {
    if (sent < 0) {
      perror("send");
    } else {
      perror("incomplete send");
    }
  }
}

void timestamp(struct message *m); // XXX - implement time stamping feature

/* Unpack struct, send each field as char stream */
int sendmessage(int sfd, struct message *msg) {
  char hash[HASH_LEN];
  hashmsg(msg, hash);

  char *tmp = "MSG:";
  trysend(sfd, tmp, sizeof tmp);
  char num[UINT64_BASE10_LEN + 1]; // +1 for ':' delimiter
  sprintf(num, "%012ld", msg->id);
  num[UINT64_BASE10_LEN] = ':';
  trysend(sfd, num, sizeof num);
  trysend(sfd, msg->from, sizeof(msg->from));
  trysend(sfd, msg->text, sizeof(msg->text));
  char flags[UINT32_BASE10_LEN + 1];
  sprintf(flags, "%012ld", msg->flags);
  flags[UINT32_BASE10_LEN] = ':';
  trysend(sfd, flags, sizeof flags);
}

// XXX rename
int recvpacket(struct msg *m, int sfd) {
  size_t received;
  if ((received = recv(sfd, m, sizeof m, 0)) != sizeof m) {
    perror("recv");
    return -1;
  }
  return 0;
}

// XXX rename to avoid confusion with sendmsg
int sendpacket(struct msg *m, int sfd) {
  size_t sent;
  if ((sent = send(sfd, m, sizeof m, 0)) != sizeof m) {
    if (sent < 0) {
      perror("send");
      return -1;
    }
    perror("incomplete send");
    return -1;
  }
  return 0;
}

/*
 * XXX - implement actual end connection protocol
 * - check for recognition of logging off
 */
void endconnection(int fd) {
  char buff[5] = "exit";
  send(fd, buff, 5, 0);
  close(fd);
}

// get sockaddr, IPv4 or IPv6
void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

/*
 * Return array of packets
 * representing a message.
 * Message is determined by '\n'-terminated input
 * from user
 * XXX - fill up stack variable first, then iff max payload is reached,
 * then allocate on heap and return a pointer to array of packets representing
 * msg. i.e. reduce memory allocation footprint.
 */
struct packet *makemsg(size_t *numpackets) {
  struct packet *ret = malloc(sizeof(*ret));
  int c, i, j;
  size_t size;
  for (i = 0; i < MAX_PACKETS_PER_MSG; i++) {
    for (j = 0; j < MAX_PACKET_PAYLOAD_SIZE; j++) {
      c = getchar(); 
      if (c == '\n') {
        *num
        goto end;
      }
      ret[i]->text[j] = c;
    }
  }
  fprintf(stderr, "Cannot store input into message\n");
  free(ret);
  return NULL;
end:
   
}

void getinput(char *dest, size_t *res, size_t len) {
  int c;
  for (int i = 0; i < len; i++) {
    c = getchar();
    if (c == '\n') {
      *res = i + 1;
      break;
    }
    dest[i] = c;
  }
  dest[*res - 1] = '\0';
}

void *thread_recv(void *sfdp) {
  int sfd = *((int *)sfdp);
  char buff[MAX_RECV_LEN];
  while (1) {
    if (recv(sfd, buff, MAX_RECV_LEN, 0) == -1) {
      perror("recv");
      break;
    }
    if (strcmp(buff, "exit") == 0) {
      printf("exiting.\n");
      endconnection(sfd);
      break;
    }
    printf("\n%s\n", buff);
  }
  return NULL; // XXX pthread_exit(retvalue);
}

void *thread_send(void *sfdp) {
  int sfd = *((int *)sfdp);
  char buff[MAX_SEND_LEN];
  size_t msglen;
  int s;

  struct pollfd listener;
  listener.fd = 0; // poll for stdin
  listener.events = POLLIN; // wait till we have input

  while (1) {
    /* Prompt and wait for input */
    printf(":");
    poll(&listener, 1, -1); // block until we can read
    if (listener.revents == POLLIN) { 
      /* Grab input, check for exit */
      getinput(buff, &msglen, MAX_SEND_LEN);
      if (strcmp(buff, "exit") == 0) {
        printf("exiting.\n");
        // XXX endconnection();
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
  struct handlerinfo *info = arg;
  pthread_t sender, receiver;

  if (pthread_create(&sender, NULL, thread_send, &info->sfd)) {
    fprintf(stderr, "Could not create message sending thread\n");
    perror("pthread_create");
    return NULL; // XXX pthread_exit(retvalue)
  }
  if (pthread_create(&receiver, NULL, thread_recv, &info->sfd)) {
    fprintf(stderr, "Could not create message receiving thread\n");
    perror("pthread_create");
    // XXX kill sender thread 
    return NULL; // XXX pthread_exit(retvalue);
  }

  // XXX failure should kill all threads
  pthread_join(sender, NULL);
  pthread_join(receiver, NULL);

  return NULL; // XXX pthread_exit(retvalue);
}
