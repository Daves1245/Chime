#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "defs.h"
#include "message.h"
#include "colors.h"
#include "user.h"

/***********
 * XXX
 * - user profiles 
 * - timestamp messages 
 * - implement message hashing
 * - chat storing (locally and server side)
 * - more user commands
 ***********/

void showmessage(const struct message *msg) {
  printf(CYAN "[%s]" ANSI_RESET ":%s\n", msg->from, msg->txt);
}

static void cmdparse(struct message *msg) {
  if (strcmp(msg->txt + 1, "exit") == 0) {
    msg->flags = FDISCONNECT;
  }
}

int packmessage(struct message *msg) {
  fgets(msg->txt, MAX_TEXT_LEN + 1, stdin);
  if (msg->txt[0] == '/') {
    cmdparse(msg);
  }
  time_t rtime;
  struct tm *now;
  time(&rtime);
  now = localtime(&rtime);
  sprintf(msg->timestmp, "[%d:%2d]", now->tm_hour, now->tm_min);
  return 0;
}

int makemessage(const struct user *usr, struct message *msg) {
  msg->uid = usr->uid;
  strcpy(msg->from, usr->handle);
  return 0;
}

int recvmessage(int sfd, struct message *msg) {
  char buff[UINT64_BASE10_LEN + UINT32_BASE10_LEN + MAX_TEXT_LEN + UINT32_BASE10_LEN + 4] = { 0 };
  size_t bread;
  char *tmp = NULL;
  bread = recv(sfd, buff, sizeof buff, 0);
  tmp = strtok(buff, "\n");
  while (!tmp) {
    bread += recv(sfd, buff + bread, sizeof(buff) - bread, 0);  
    tmp = strtok(NULL, "\n");
  }
  msg->id = atoll(tmp);
  tmp = strtok(NULL, "\n");
  while (!tmp) {
    bread += recv(sfd, buff + bread, sizeof(buff) - bread, 0);
    tmp = strtok(NULL, "\n");
  }
  msg->uid = atoll(tmp);
  tmp = strtok(NULL, "\n");
  while (!tmp) {
    bread += recv(sfd, buff + bread, sizeof(buff) - bread, 0);
    tmp = strtok(NULL, "\n");
  }
  strcpy(msg->from, tmp);
  tmp = strtok(NULL, "\n");
  while (!tmp) {
    bread += recv(sfd, buff + bread, sizeof(buff) - bread, 0);
    tmp = strtok(NULL, "\n");
  }
  strcpy(msg->txt, tmp);
  tmp = strtok(NULL, "\n");
  while (!tmp) {
    bread += recv(sfd, buff + bread, sizeof(buff) - bread, 0);
    tmp = strtok(NULL, "\n");
  }
  msg->flags = atoll(tmp);
  return 0;
}

/* Unpack struct, send each field as char stream */
int sendmessage(int fd, const struct message *msg) {
  /* One buffer large enough to store each field - and their respective null byte */
  char buff[UINT64_BASE10_LEN + UINT32_BASE10_LEN + HANDLE_LEN + MAX_TEXT_LEN + UINT32_BASE10_LEN + 5];
  memset(buff, 0, sizeof buff);
  sprintf(buff, "%" PRIu64 "\n%" PRIu32 "\n%s\n%s\n%" PRIu32 "\n%c", msg->id, msg->uid, msg->from, msg->txt, msg->flags, '\0');
  size_t sent = 0, tosend = strlen(buff);
  while ((sent = send(fd, buff, tosend - sent, 0)) != tosend) {
    if (sent < 0) {
      perror("send");
      exit(EXIT_FAILURE);
    } else if (sent == 0) {
      fprintf(stderr, "Connection has closed. Cannot sent message. Exiting now...\n");
      exit(EXIT_FAILURE);
    }
  }
  return 0;
}

