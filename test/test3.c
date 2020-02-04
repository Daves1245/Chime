#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

struct info {
  int n;
  char *name;
  int sleep_time;
};

void *threadlp(void *arg) {
  struct info *tinfo = arg;
  printf("[START]: %s\n", tinfo->name);
  for (int i = 0; i < tinfo->n; i++) {
    printf("%s\n", tinfo->name);
    sleep(tinfo->sleep_time);
  }
  printf("[DONE]: %s\n", tinfo->name);
  return NULL;
}

int main() {
  pthread_t one, two;
  struct info ione = {5, "ONE", 1}, itwo = {5, "TWO", 2};
  int sone, stwo;

  /* Create the threads */
  sone = pthread_create(&one, NULL, threadlp, &ione);
  stwo = pthread_create(&two, NULL, threadlp, &itwo);

  /* Wait for completion: join with main function */
  pthread_join(one, NULL);
  pthread_join(two, NULL);

  /* Exit gracefully :) */
  printf("exiting main\n");
  return 0;
}
