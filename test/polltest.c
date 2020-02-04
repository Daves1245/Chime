#include <stdio.h>
#include <poll.h>

#define MILLISTOSEC 1000

int main() {
  struct pollfd tmp;

  tmp.fd = 0;
  tmp.events = POLLIN;

  int res = poll(&tmp, 1, 10 * MILLISTOSEC);

  printf("result: %d\n", res);
  printf("revents: %d\n", tmp.revents);

  if (tmp.revents & POLLIN) {
    printf("success!\n");
  }
  printf("exiting main\n");
  return 0;
}
