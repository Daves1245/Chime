#ifndef TRANSMITMSG_H
#define TRANSMITMSG_H

#include <inttypes.h>
#include <time.h>
#include <errno.h>

#include "user.h"
#include "status.h"
#include "types.h"
#include "message.h"
#include "connection.h"

status recvmessage_conn(const struct connection *conn, struct message *msg);                  /* Receive a message coming in from sfd */
status sendmessage_conn(const struct connection *conn, const struct message *msg);            /* Unpack a message, send it through fd */
status recvmessage(int sfd, struct message *msg);
status sendmessage(int sfd, const struct message *msg);
status makemessage(const struct user *usr, struct message *msg);   /* Put user and other information in a message */
status packmessage(struct message *msg);                           /* Pack msg with appropriate info */
status cmdparse(struct message *msg);

#endif
