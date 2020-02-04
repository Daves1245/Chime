#include <stdio.h>
#include <time.h>
#include <errno.h>

int main() {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  printf("[%d:%d]\n", t->tm_hour, t->tm_min);

  return 0;
}
