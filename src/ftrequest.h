#ifndef FILE_TRANSFER_REQUEST_H
#define FILE_TRANSFER_REQUEST_H

#include "connection.h"
#include "fileinfo.h"

struct ftrequest {
  struct connection *conn;
  struct fileinfo finfo;
};

#endif
