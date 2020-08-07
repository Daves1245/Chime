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
 * - user profiles 
 * - timestamp messages 
 * - implement message hashing
 * - chat storing (locally and server side)
 * - more user commands
 ***********/

// it's in another file!
void debugmessage(const struct message *m) {
  printf("---MESSAGE---\n");
  printf("[id]: `%d`\n", m->id);
  printf("[uid]: `%d`\n", m->uid);
  printf("[timestamp]: `%s`\n", m->timestmp);
  printf("[from]: `%s`\n", m->from);
  printf("[txt]: `%s`\n", m->txt);
  printf("[flags]:`%d`\n", m->flags);
}

void showmessage(const struct message *msg) {
  printf(CYAN "[%s]" ANSI_RESET ":%s\n", msg->from, msg->txt);
}

STATUS timestampmessage(struct message *msg) {
  time_t rtime;
  struct tm *now;
  time(&rtime);
  now = localtime(&rtime);
  sprintf(msg->timestmp, "[%d:%2d]", now->tm_hour, now->tm_min);
  return 0;
}

STATUS packmessage(struct message *msg) {
  fgets(msg->txt, MAX_TEXT_LEN + 1, stdin);
  if (msg->txt[0] == '/') {
    cmdparse(msg);
  }
  timestampmessage(msg);
  return 0;
}

STATUS makemessage(const struct user *usr, struct message *msg) {
  msg->uid = usr->uid;
  strcpy(msg->from, usr->handle);
  return 0;
}
