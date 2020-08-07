#ifndef TRANSMITMSG_H
#define TRANSMITMSG_H

#include <inttypes.h>
#include <time.h>

#include "user.h"
#include "status.h"
#include "types.h"
#include "message.h"
#include "connection.h"

STATUS recvmessage_conn(const struct connection *conn, struct message *msg);                  /* Receive a message coming in from sfd */
STATUS sendmessage_conn(const struct connection *conn, const struct message *msg);            /* Unpack a message, send it through fd */
STATUS recvmessage(int sfd, struct message *msg);
STATUS sendmessage(int sfd, const struct message *msg);
STATUS makemessage(const struct user *usr, struct message *msg);   /* Put user and other information in a message */
STATUS packmessage(struct message *msg);                           /* Pack msg with appropriate info */
STATUS cmdparse(struct message *msg);

#endif
