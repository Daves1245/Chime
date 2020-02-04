#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

void *threadedloop(void *arg) {
  printf("[START]: Thread\n");
  int n = *((int *) arg);
  for (int i = 0; i < n; i++) {
    printf("thread\n");
    sleep(1);
  }
  return NULL;
}

int main() {
  pthread_t thread;
  int s;

  /* Create threaded loop */
  int arg = 5;
  s = pthread_create(&thread, NULL, threadedloop, &arg);
  if (s != 0) { // Error checking
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }
  /* Wait for thread to terminate: join with main function */
  pthread_join(thread, NULL);
  return 0;
}
