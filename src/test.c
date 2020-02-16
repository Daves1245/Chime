#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <poll.h>

#include "common.h"

#define PORT "33401"
#define MAXDATASIZE 100

int main(int argc, char **argv) {
  printf("%ld\n", sizeof(struct message));
  return 0;
}
