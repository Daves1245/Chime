#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

#include "defs.h"
#include "colors.h"
#include "user.h"
#include "status.h"
#include "types.h"
#include "message.h"
#include "transmitmsg.h"
#include "connection.h"

/***********
 * XXX
 * - user profiles 
 * - timestamp messages 
 * - implement message hashing
 * - chat storing (locally and server side)
 * - more user commands
 ***********/

// it's in another file!
extern STATUS uploadfile(struct connection *conn, int fd);

STATUS cmdparse(struct message *msg) {
  char *cmd, buff[MAX_TEXT_LEN + 1];
  strcpy(buff, msg->txt + 1);
  strtok(buff, " ");
  cmd = strtok(NULL, " ");
  if (cmd) { // commands with arguments
    if (strcmp(cmd, "upload") == 0) {
      char *filepath = strtok(NULL, " ");
      if (!filepath) {
        fprintf(stderr, RED "usage: /upload [file]\n");
        return ERROR_INVALID_FILEPATH;
      }
      int fd = open(filepath, O_RDONLY);
      if (fd < 0) {
        fprintf(stderr, RED "file path must be valid");
        perror("open");
        return ERROR_INVALID_FILEPATH;
      }
    }
  } else { // commands without arguments
    if (strcmp(msg->txt + 1, "exit") == 0) {
      msg->flags = FDISCONNECT;
    }
  }
  return OK;
}

// TODO fix
STATUS recvmessage(int sfd, struct message *msg) {
  char buff[UINT64_BASE10_LEN + UINT32_BASE10_LEN + MAX_TEXT_LEN + UINT32_BASE10_LEN + 4] = { 0 };
  size_t bread;
  char *tmp = NULL;
  int len = 0;
  bread = recv(sfd, buff, sizeof buff, 0);
  tmp = strtok(buff, "\n");
  while (!tmp) {
    len = recv(sfd, buff + bread, sizeof(buff) - bread, 0);  
    if (len == 0) {
      return ERROR_CONNECTION_LOST;
    }
    bread += len;
    tmp = strtok(NULL, "\n");
  }
  msg->id = atoll(tmp);
  tmp = strtok(NULL, "\n");
  while (!tmp) {
    len = recv(sfd, buff + bread, sizeof(buff) - bread, 0);  
    if (len == 0) {
      return ERROR_CONNECTION_LOST;
    }
    bread += len;
    tmp = strtok(NULL, "\n");
  }
  msg->uid = atoll(tmp);
  tmp = strtok(NULL, "\n");
  while (!tmp) {
    len = recv(sfd, buff + bread, sizeof(buff) - bread, 0);  
    if (len == 0) {
      return ERROR_CONNECTION_LOST;
    }
    bread += len;
    tmp = strtok(NULL, "\n");
  }
  strcpy(msg->from, tmp);
  tmp = strtok(NULL, "\n");
  while (!tmp) {
    len = recv(sfd, buff + bread, sizeof(buff) - bread, 0);  
    if (len == 0) {
      return ERROR_CONNECTION_LOST;
    }
    bread += len;
    tmp = strtok(NULL, "\n");
  }
  strcpy(msg->txt, tmp);
  tmp = strtok(NULL, "\n");
  while (!tmp) {
    len = recv(sfd, buff + bread, sizeof(buff) - bread, 0);  
    if (len == 0) {
      return ERROR_CONNECTION_LOST;
    }
    bread += len;
    tmp = strtok(NULL, "\n");
  }
  msg->flags = atoll(tmp);
  return OK;
}

/* Unpack struct, send each field as char stream */
STATUS sendmessage(int sfd, const struct message *msg) {
  /* One buffer large enough to store each field - and their respective null byte */
  char buff[UINT64_BASE10_LEN + UINT32_BASE10_LEN + HANDLE_LEN + MAX_TEXT_LEN + UINT32_BASE10_LEN + 5];
  memset(buff, 0, sizeof buff);
  sprintf(buff, "%d\n%d\n%s\n%s\n%d\n%c", msg->id, msg->uid, msg->from, msg->txt, msg->flags, '\0');
  size_t sent = 0, tosend = strlen(buff);
  while ((sent = send(sfd, buff, tosend - sent, 0)) != tosend) {
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

