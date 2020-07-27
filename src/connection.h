#ifndef CONNECTION_H
#define CONNECTION_H

#include "user.h"

struct connection {
    struct user usr;
    int fd;
};

#endif
