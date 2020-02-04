#include <stdio.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h> 

struct tinfo {
  int n;
  char *name;
  int sleep_time;
};

#define MAXBUFFLEN 100

void getinput(char *dest, size_t *res, size_t len) {
  register int c;
  for (int i = 0; i < len; i++) {
    c = getchar();
    if (c == '\n') {
      *res = i + 1;
      break;
    }
    dest[i] = c;
  }
  
  dest[*res - 1] = '\0';
}

void *tin(void *arg) {
  struct tinfo *info = arg;
  struct pollfd tmp;
  char buff[MAXBUFFLEN];
  size_t res;

  tmp.fd = 0;
  tmp.events = POLLIN;

  printf("[START]: %s\n", info->name);

  for (int i = 0; i < info->n; i++) {
    poll(&tmp, 1, 1);
    if (tmp.revents & POLLIN) {
      getinput(buff, &res, MAXBUFFLEN);      
      printf(">> received: %s\n", buff);
    } else {
      printf("polled\n");
    }
    sleep(info->sleep_time);
  }

  printf("[END]: %s\n", info->name);
  return NULL;
}

void *tout(void *arg) {
  struct tinfo *info = arg;
  printf("[START]: %s\n", info->name);
  for (int i = 0; i < info->n; i++) {
    printf("[SIMULATED MESSAGE FROM USER]\n");
    sleep(info->sleep_time);
  }
  printf("[END]: %s\n", info->name);
  return NULL;
}

int main() {
  pthread_t one, two;
  struct tinfo ione = {5, "INPUT", 2}, itwo = {5, "OUTPUT", 2};
  int sone, stwo;

  sone = pthread_create(&one, NULL, tin, &ione);
  stwo = pthread_create(&two, NULL, tout, &itwo);

  if (sone != 0 || stwo != 0) {
    perror("pthread_create");
  }

  pthread_join(one, NULL);
  pthread_join(two, NULL);

  printf("exiting main\n");

  return 0;
}
