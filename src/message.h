#ifndef MESSAGE_H
#define MESSAGE_H

#include <inttypes.h>
#include <time.h>
#include <signal.h>
#include <math.h>

struct message {
  uint64_t id;      /* Unique message id */
  struct tm ts;     /* Message timestamp */
  uint32_t uid;  /* ID of user that message belongs to */
  char from[HANDLE_LEN + 1];
  char *txt;        /* txt of the message */
  size_t txtlen;    /* length of string txt */
  uint32_t flags;   /* message flags */
};

void hashmsg(struct message *msg, char *res);         /* Hash a message and store it in res */
void timestamp(struct message *m);                    /* XXX - Timestamp a message */
void displaymessage(const struct message *msg);       /* Display a message to the screen */
int recvmessage(int sfd, struct message *msg);        /* Receive a message coming in from sfd */
int sendmessage(int sfd, const struct message *msg);  /* Unpack a message, send it through fd */

#endif
