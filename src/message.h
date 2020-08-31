#ifndef MESSAGE_H
#define MESSAGE_H

#include <inttypes.h>
#include <time.h>

#include "user.h"
#include "status.h"
#include "types.h"

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
status timestampmessage(struct message *msg);                      /* Time stamp a message */

#endif
