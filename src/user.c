#include "user.h"

struct user {
  int uid; /* This user's id */
  char handle[HANDLE_LEN + 1]; /* Username */
  int *friends; /* array of friend uids */
};
