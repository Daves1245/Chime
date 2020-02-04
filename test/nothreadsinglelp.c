#include <unistd.h>
#include <stdio.h>

void nothreadlp(int n) {
  printf("[START]\n");
  for (int i = 0; i < n; i++) {
    sleep(1);
    printf("1\n");
  }
  printf("[END]\n");
}

int main() {
  nothreadlp(5); 
  return 0;
}
