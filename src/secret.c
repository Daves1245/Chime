#include "secret.h"

/* Generate a secret for authentication
 * This is obviously not secure, and
 * must be changed soon */

long gen_secret(void) {
  return MAX + rand() / (RAND_MAX / (MIN - MAX + 1) + 1);
}
