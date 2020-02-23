#include "user.h"

struct user {
  uint16_t userid;
  char handle[HANDLE_LEN + 1];
  // XXX friends
};
