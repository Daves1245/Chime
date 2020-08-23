#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "connection.h"
#include "defs.h"
#include "user.h"
#include "message.h"
#include "transmitmsg.h"

STATUS chime_connect(struct connection *conn) {
  return OK;
}

// TODO use the full extent of the connection wrapper
// TODO make a disconnect user for server, disconnect for client
void disconnect(struct connection *conn) {
  struct message fin;
  memset(&fin, 0, sizeof fin);
  strcpy(fin.txt, "/exit");
  strcpy(fin.from, "server"); // TODO fix
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

void disconnect_wrapper_and_exit(int sfd) {
  disconnect_wrapper(sfd);
  printf("\nDisconnected\n");
  _Exit(EXIT_SUCCESS);
}

void disconnect_and_exit(struct connection *conn) {
  disconnect(conn);
  printf("\nDisconnected\n");
  _Exit(EXIT_SUCCESS);
}
