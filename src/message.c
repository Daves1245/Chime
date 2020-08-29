#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

#include "defs.h"
#include "message.h"
#include "colors.h"
#include "user.h"
#include "status.h"
#include "types.h"
#include "transmitmsg.h"

/***********
 * XXX
 * - implement message hashing
 * - chat storing (locally and server side)
 * - more user commands
 ***********/

void debugmessage(const struct message *m) {
  printf("---MESSAGE---\n");
  printf("[id]: `%d`\n", m->id);
  printf("[uid]: `%d`\n", m->uid);
  printf("[timestamp]: `%s`\n", m->timestmp);
  printf("[from]: `%s`\n", m->from);
  printf("[txt]: `%s`\n", m->txt);
  printf("[flags]:`%d`\n", m->flags);
}

/*
 * showmessage() - display a message to stdout
 * @msg: the message to display
 *
 * TODO account for terminals without color escape codes
 */
void showmessage(const struct message *msg) {
  printf(CYAN "[%s]" ANSI_RESET ":%s\n", msg->from, msg->txt);
}

/*
 * timestampmessage() - store the current timestamp
 * in a message
 *
 * Return: OK on success
 */
STATUS timestampmessage(struct message *msg) {
  time_t rtime;
  struct tm *now;
  time(&rtime);
  now = localtime(&rtime);
  sprintf(msg->timestmp, "[%d:%2d]", now->tm_hour, now->tm_min);
  return OK;
}

/*
 * makemessage() - make a message
 * @usr: user info to store into msg
 *
 *    Given a message and static information to pack it with,
 * makemessage fills in the fields of msg that are static.
 * As in, the difference between makemessage and packmessage
 * is that the fields in packmessage are volatile and will
 * be overwritten on the next message sent, while those 
 * written in makemessage are supposed to stay static throughout
 * the entire session. This includes the user id and handle.
 *
 * Return: OK on success
 */
STATUS makemessage(const struct user *usr, struct message *msg) {
  msg->uid = usr->uid;
  strcpy(msg->from, usr->handle);
  return OK;
}


