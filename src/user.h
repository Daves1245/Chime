#ifndef USER_H
#define USER_H

#include "defs.h"

struct user {
  int uid;
  char handle[HANDLE_LEN + 1];
  // XXX friends
};

#endif
