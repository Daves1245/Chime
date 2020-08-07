#ifndef CONNECTION_H
#define CONNECTION_H

#include <unistd.h>

#include "user.h"
#include "status.h"
#include "types.h"

struct connection {
    int sfd;                        /* Socket that user is connected on */
    struct user uinfo;              /* Info about user */
    struct connection *next, *prev; /* Make this a linked list */
};

STATUS chime_connect(struct connection *conn);

// TODO use the full extent of the connection wrapper
void disconnect(struct connection *conn);
void disconnect_wrapper(int sfd);
void disconnect_wrapper_and_exit(int sfd);
void disconnect_and_exit(struct connection *conn);

#endif
