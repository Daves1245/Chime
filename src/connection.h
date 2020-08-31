#ifndef CONNECTION_H
#define CONNECTION_H

#include <unistd.h>

#include "user.h"
#include "status.h"
#include "types.h"

/*
 * struct connection - represent a connection associated with a client
 * @uinfo: user info of the client associated with this connection
 * @next: pointer to next connection entry in linked list
 * @prev: pointer to previous connection entry in linked list
 * TODO - remove linked list
 */
struct connection {
    int sfd;                        /* This user's socket for messaging */
    int transferfd;                 /* This user's socket for file transfers */
    long secret;                    /* Used to verify authentication over multiple sockets */
    struct user uinfo;              /* Info about user */
    struct connection *next, *prev; /* Make this a linked list */
};

status chime_connect(struct connection *conn);

// TODO use the full extent of the connection wrapper
void disconnect(struct connection *conn);
void disconnect_wrapper(int sfd);
void disconnect_wrapper_and_exit(int sfd);
void disconnect_and_exit(struct connection *conn);

#endif
