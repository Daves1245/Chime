#ifndef CONNECTION_H
#define CONNECTION_H

#include <unistd.h>

#include "user.h"
#include "message.h"
#include "status.h"
#include "types.h"

struct connection {
    int sfd;                        /* Socket that user is connected on */
    struct user uinfo;              /* Info about user */
    struct connection *next, *prev; /* Make this a linked list */
};

void chime_connect() {

}


// TODO use the full extent of the connection wrapper
void disconnect(struct connection *conn) {
  struct message fin;
  memset(&fin, 0, sizeof fin);
  strcpy(fin.txt, "/exit");
  timestampmessage(&fin);
  fin.flags = FDISCONNECT;
  sendmessage(conn->sfd, &fin);
  close(conn->sfd);
}

void disconnect_wrapper(int sfd) {
  struct connection conn;
  conn.sfd = sfd;
  disconnect(&conn);
}

void disconnect_and_exit(struct connection *conn) {
  disconnect(conn);
  printf("\nDisconnected\n");
  _Exit(EXIT_SUCCESS);
}

#endif
