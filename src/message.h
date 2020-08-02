#ifndef MESSAGE_H
#define MESSAGE_H

#include <inttypes.h>
#include <time.h>

#include "user.h"

#define TS_LEN 20

struct message {
  int id;      /* Unique message id */
  int uid;  /* ID of user that message belongs to */
  char timestmp[TS_LEN];
  char from[HANDLE_LEN + 1];
  char txt[MAX_TEXT_LEN + 1]; /* Text message being sent */
  int flags;
};

void hashmsg(struct message *msg, char *res);                   /* Hash a message and store it in res */
void timestamp(struct message *m);                              /* XXX - Timestamp a message */
void showmessage(const struct message *msg);                    /* Display a message to the screen */
int recvmessage(int sfd, struct message *msg);                  /* Receive a message coming in from sfd */
int sendmessage(int sfd, const struct message *msg);            /* Unpack a message, send it through fd */
int makemessage(const struct user *usr, struct message *msg);   /* Put user and other information in a message */
int packmessage(struct message *msg);                           /* Pack msg with appropriate info */
int timestampmessage(struct message *msg);                      /* Time stamp a message */

#endif
