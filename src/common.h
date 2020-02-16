#include <inttypes.h>
#include <time.h>
#include <signal.h>

#include "colors.h"

#include "defs.h"

/***********
 * XXX TODO
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
  uint64_t id;
  char from[MAX_NAME_LEN + 1];
  char text[MAX_TEXT_LEN + 1];
  uint32_t flags;
};

struct user {
  uint16_t userid;
  char handle[MAX_NAME_LEN + 1];
  // XXX friends
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

static inline void initmsg(int sfd) {
  send(sfd, "MSG:", 4, 0);
}

/* Unpack struct, send each field as char stream */
int sendmessage(int sfd, struct message *msg) {
  //initmsg(sfd);

  char hash[HASH_LEN];
  char id[UINT64_BASE10_LEN + 1]; // +1 for ':' delimiter
  char flags[UINT32_BASE10_LEN + 1];

  hashmsg(msg, hash);

  sprintf(id, "%012" PRIu64, msg->id);
  sprintf(flags, "%012" PRIu32, msg->flags);

  id[UINT64_BASE10_LEN] = ':';
  flags[UINT32_BASE10_LEN] = ':';

  send(sfd, id, sizeof id, 0);
  send(sfd, msg->from, MAX_NAME_LEN + 1, 0);
  send(sfd, msg->text, MAX_TEXT_LEN + 1, 0);
  send(sfd, flags, sizeof flags, 0);
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
  printf("[MSG] id %" PRIu64 "\n", msg->id);
  printf("[MSG] from %s\n", msg->from);
  printf("[MSG] text %s\n", msg->text);
  printf("[MSG] flags %" PRIu32 "\n", msg->flags);
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
  while (1) {
    recvmessage(info->sfd, &msg);
#ifdef DEBUG
    puts("[THREAD RECV]");
    printf("[MSG] id %" PRIu64 "\n", msg.id);
    printf("[MSG] from %s\n", msg.from);
    printf("[MSG] text %s\n", msg.text);
    printf("[MSG] flags %" PRIu32 "\n", msg.flags);
#endif
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
	memcpy(msg.from, info->handle, strlen(info->handle));

	struct pollfd listener;
	listener.fd = 0; // poll for stdin
	listener.events = POLLIN; // wait till we have input

	while (1) {
		poll(&listener, 1, -1); // block until we can read
		if (listener.revents == POLLIN) {
			/* XXX Grab input, check for exit */
			getinput(msg.text, &msgtextlen, MAX_TEXT_LEN);
			if (msgtextlen == 0) {
				memcpy(msg.text, "/exit", 5);
			}
			msg.id++;
			sendmessage(info->sfd, &msg);
			if (strcmp(msg.text, "/exit") == 0) {
				exit(EXIT_SUCCESS);
			}
		}
	}
}
