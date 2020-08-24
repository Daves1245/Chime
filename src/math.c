#include "math.h"

/* The standard library doesn't
 * have these so we include them
 * here */
int min(int a, int b) {
  return a < b ? a : b;
}

int max(int a, int b) {
  return a > b ? a : b;
}
