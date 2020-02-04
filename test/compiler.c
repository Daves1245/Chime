#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s [filename]\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  char command[10000];
  sprintf(command, "gcc %s -o test -Wall -lpthread", argv[1]);
  system(command);
  return 0;
}
