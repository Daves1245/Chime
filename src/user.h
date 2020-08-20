#ifndef USER_H
#define USER_H

#include "defs.h"

/*
 * struct user - represent a currently connected client and
 * their necessary info
 * @uid: the user's identification number
 * @handle: the user's handle
 */
struct user {
  int uid;
  char handle[HANDLE_LEN + 1];
  // XXX friends
};

#endif
