#ifndef MESSAGE_H
#define MESSAGE_H

#include <inttypes.h>
#include <time.h>

#include "user.h"
#include "status.h"
#include "types.h"

#define TIMESTAMP_LEN 20

struct message {
  int id;      /* Unique message id */
  int uid;  /* ID of user that message belongs to */
  char timestmp[TIMESTAMP_LEN + 1];
  char from[HANDLE_LEN + 1];
  char txt[MAX_TEXT_LEN + 1]; /* Text message being sent */
  int flags;   /* message flags */
};

void hashmsg(struct message *msg, char *res);                   /* Hash a message and store it in res */
void timestamp(struct message *m);                              /* XXX - Timestamp a message */
void showmessage(const struct message *msg);                    /* Display a message to the screen */
void debugmessage(const struct message *msg);                   /* Debug print a message */
STATUS recvmessage(int sfd, struct message *msg);                  /* Receive a message coming in from sfd */
STATUS sendmessage(int sfd, const struct message *msg);            /* Unpack a message, send it through fd */
STATUS makemessage(const struct user *usr, struct message *msg);   /* Put user and other information in a message */
STATUS packmessage(struct message *msg);                           /* Pack msg with appropriate info */
STATUS timestampmessage(struct message *msg);                      /* Time stamp a message */

#endif
