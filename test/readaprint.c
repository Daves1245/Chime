#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

void *thread1(void *arg) {
  printf("[START]: Thread 1\n");
  for (int i = 0; i < 5; i++) {
    sleep(1);
    printf("1\n");
  }
  return NULL;
}

void *thread2(void *arg) {
  printf("[START]: Thread 2\n");
  for (int i = 0; i < 3; i++) {
    sleep(1);
    printf("2\n");
  }
  return NULL;
}

int main() {
  pthread_t two, one; 

  one = pthread_create(&one, NULL, thread1, NULL);
  two = pthread_create(&two, NULL, thread2, NULL);
  
  int a, b;
  a = pthread_join(one, NULL);
  b = pthread_join(two, NULL);
  printf("%d %d %d\n", a == EINVAL, a == EDEADLK, a == ESRCH);

  printf("main: done\n");
  return 0;
}
