#ifndef USER_H
#define USER_H

struct user {
  int uid;
  char handle[HANDLE_LEN + 1];
  // XXX friends
};

#endif
