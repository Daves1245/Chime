#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void *test(void *arg) {
  for (int i = 0; i < 10; i++) {
    sleep(3);
    pthread_mutex_lock(&lock);
    printf("printing from thread :)");
    pthread_mutex_unlock(&lock);
  }
  return NULL;
}

#define BUFFLEN 100
void *test2(void *arg) {
  char buff[BUFFLEN];
  while (1) {
    pthread_mutex_lock(&lock);
    printf(":");
    pthread_mutex_unlock(&lock);
    scanf("%s\n", buff);
    if (strcmp(buff, "exit") == 0) {
      printf("exiting.\n");
      break;
    }
    pthread_mutex_lock(&lock);
    printf("%s\n", buff);
    pthread_mutex_unlock(&lock);
  }
  return NULL;
}

int main() {
  pthread_t a, b;
  int sa, sb;
  if ((sa = pthread_create(&a, NULL, test, NULL)) != 0) {
    perror("pthread_create");
  }
  if ((sb = pthread_create(&b, NULL, test2, NULL)) != 0) {
    perror("pthread_create");
  }

  pthread_join(b, NULL);
  return 0;
}
