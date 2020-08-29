#ifndef FILE_TRANSFER_REQUEST_H
#define FILE_TRANSFER_REQUEST_H

#include "connection.h"
#include "fileinfo.h"

struct ftrequest {
  struct connection *conn; /* Connection on which transfer will take place */
  struct fileinfo finfo; /* File header and other info */
  int broadcast; /* Whether the file should then be broadcast across all users */
};

#endif
