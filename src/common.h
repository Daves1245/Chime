#include <inttypes.h>
#include <time.h>
#include <signal.h>
#include <math.h>

#include "colors.h"

#include "defs.h"

/***********
 * XXX
 * - user profiles 
 * - timestamp messages 
 * - implement message hashing
 * - chat storing (locally and server side)
 * - more user commands
 ***********/

int sfd;

struct handlerinfo {
  int sfd;
  char *handle;
};

struct message {
  uint64_t id;      /* Unique message id */
  struct tm ts;     /* Message timestamp */
  uint32_t uid;  /* ID of user that message belongs to */
  char from[HANDLE_LEN + 1];
  char *txt;        /* txt of the message */
  size_t txtlen;    /* length of string txt */
  uint32_t flags;   /* message flags */
};

struct user {
  uint16_t userid;
  char handle[HANDLE_LEN + 1];
  // XXX friends
};

void endconnection();                               /* Terminate the connection */
void getinput(char *dest, size_t *res, size_t len); /* store '\n'-terminated line at most len into dest and modify *res accordingly */
void *thread_recv(void *);                          /* Message receiving thread */
void *thread_send(void *);                          /* Message sending thread */
void *connection_handler(void *);                   /* Connection handling thread */
void hashmsg(struct message *msg, char *res);       /* Hash a message and store it in res */
int sendmessage(int sfd, const struct message *msg);      /* Send a message to sfd */
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

int numdigs(uint64_t num) {
  int ret = 0;
  while (num > 0) {
    ret++;
    num /= 10;
  }
  return ret;
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

void reversestr(char *str, size_t len) {
  for (int i = 0; i < len / 2; i++) {
    int tmp = str[i];
    str[i] = str[len - 1 - i];
    str[len - 1 - i] = tmp;
  }
}

void displaymessage(const struct message *msg) {
  printf(CYAN "[%s]" ANSI_RESET ":%s\n", msg->from, msg->txt);
}

/*
 * XXX - implement actual end connection protocol
 * - check for recognition of logging off
 */
void endconnection(void) {
  struct message tmp;
  memset(&tmp, 0, sizeof tmp);
  tmp.flags = FDISCONNECT;
  trysend(sfd, &tmp, sizeof tmp);
  close(sfd);
  printf("Disconnected\n");
  exit(EXIT_SUCCESS);
}

/*
 *the more i work on this the more i realize i'm
 just making a bad IRC clone for C.
 */
// XXX
void sighandler(int s) {
  if (s == SIGTERM) {
    endconnection();
  }
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

void *thread_recv(void *handlei) {
  struct handlerinfo *info = handlei;
  struct message msg;
  msg.txt = malloc(MAX_TEXT_LEN); /* XXX _guarantee_ this is freed */

  strcpy(msg.from, info->handle);
  msg.flags = FCONNECT;
  sendmessage(info->sfd, &msg);

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
        displaymessage(&msg);
		    break;
      default:
        printf(RED "[invalid flags, defaulting to displaymsg]" ANSI_RESET "\n");
        displaymessage(&msg);
        break;
    }
  }
}

void *thread_send(void *handlei) {
	struct handlerinfo *info = handlei;
	struct message msg;
	size_t msgtxtlen;

	memset(&msg, 0, sizeof msg);
	memcpy(msg.from, info->handle, strlen(info->handle));
  msg.id = 0;
  msg.uid = 1;
  msg.txt = malloc(MAX_TEXT_LEN);
  msg.flags = FMSG;

	struct pollfd listener;
	listener.fd = 0; // poll for stdin
	listener.events = POLLIN; // wait till we have input

	while (1) {
		poll(&listener, 1, -1); // block until we can read
		if (listener.revents == POLLIN) {
			/* XXX Grab input, check for exit */
			getinput(msg.txt, &msgtxtlen, MAX_TEXT_LEN);
			if (msgtxtlen == 0) {
				memcpy(msg.txt, "/exit", 5);
			}
			msg.id++;
			sendmessage(info->sfd, &msg);
			if (strcmp(msg.txt, "/exit") == 0) {
				exit(EXIT_SUCCESS);
			}
		}
	}
}

void displaybuff(char *buff, int n) {
  for (int i = 0; i < n; i++) {
    if (buff[i] == '\0') {
      putchar('_');
    } else {
      putchar(buff[i]);
    }
  }
}

int recvmessage(int sfd, struct message *msg) {
  char buff[UINT64_BASE10_LEN + UINT32_BASE10_LEN + MAX_TEXT_LEN + UINT32_BASE10_LEN + 4];
  size_t recvd = recv(sfd, buff, sizeof buff, 0);

#ifdef DEBUG
  printf("bytes received: %lu\n", recvd);
#endif

  if (recvd < 0) {
    perror("recv");
    return -1;
  } else if (recvd == 0) {
    fprintf(stderr, "Connection has closed, cannot receive message");
    return -2;
  }
  register int i = 0;
  /* XXX check i stays within sizeof buff always */
  msg->id = atoll(buff);
  while (buff[i++] != '\0') {
    while (i > recvd) {
      recvd += recv(sfd, buff + i, sizeof(buff) - i, 0);
    }
  }
  msg->uid = atoll(buff + i);
  while (buff[i++] != '\0') {
    while (i > recvd) {
    recvd += recv(sfd, buff + i, sizeof(buff) - i, 0);
    }
  }
  strcpy(msg->from, buff + i);
  while (buff[i++] != '\0') {
    while (i > recvd) {
      recvd += recv(sfd, buff + i, sizeof(buff) - i, 0);
    }
  }
  msg->txtlen = strlen(buff + i);
  strcpy(msg->txt, buff + i);
  while (buff[i++] != '\0') {
    while (i > recvd) {
      recvd += recv(sfd, buff + i, sizeof(buff) - i, 0);
    }
  }
  msg->flags = atoll(buff + i);
  return 0;
}

/* Unpack struct, send each field as char stream */
int sendmessage(int fd, const struct message *msg) {
  /* One buffer large enough to store each field - and their respective null byte */
  char buff[UINT64_BASE10_LEN + UINT32_BASE10_LEN + HANDLE_LEN + MAX_TEXT_LEN + UINT32_BASE10_LEN + 4];
  memset(buff, 0, sizeof buff);

  /* Store info in buffer, send it off in one go */
  sprintf(buff, "%" PRIu64, msg->id);
  sprintf(buff + numdigs(msg->id) + 1, "%" PRIu32, msg->uid);
  sprintf(buff + numdigs(msg->id) + numdigs(msg->uid) + 2, "%s", msg->from);
  sprintf(buff + numdigs(msg->id) + numdigs(msg->uid) + strlen(msg->from) + 3, "%s", msg->txt);
  sprintf(buff + numdigs(msg->id) + numdigs(msg->uid) + + strlen(msg->from) + msg->txtlen + 4, "%" PRIu32, msg->flags);
  displaymessage(msg);
  size_t to_send = numdigs(msg->id) + numdigs(msg->uid) + strlen(msg->from) + msg->txtlen + numdigs(msg->flags) + 5;
  size_t sent = send(fd, buff, numdigs(msg->id) + numdigs(msg->uid) + strlen(msg->from) + msg->txtlen + numdigs(msg->flags) + 5, 0);
#ifdef DEBUG
  printf("bytes to be sent: %ld\n", to_send);
  printf("bytes sent: %ld\n", sent);
  displaybuff((char *) msg, to_send);
#endif
  return 0;
}
