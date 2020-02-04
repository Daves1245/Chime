#include <stdio.h>
#include <stdlib.h>

int main() {
  char test[13];
  int n;
  do {
    printf(":");
    scanf("%d", &n);
    sprintf(test, "%012d", n); 
    test[12] = ':';
    printf("`%s`\n", test);
    printf("%d\n", atoi(test));
  } while (n != -1);
  return 0;
}
