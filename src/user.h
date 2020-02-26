#ifndef USER_H
#define USER_H

struct user {
  uint16_t uid;
  char handle[HANDLE_LEN + 1];
  // XXX friends
};

#endif
