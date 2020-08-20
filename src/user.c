#include "user.h"

struct user {
  int uid;
  char handle[HANDLE_LEN + 1];
  // TODO friends
};
