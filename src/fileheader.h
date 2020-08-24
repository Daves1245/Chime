#ifndef FILEHEADER_H
#define FILEHEADER_H

#include "defs.h"

struct fileheader {
  char filename[FILENAME_LEN + 1];
  size_t size;
};

#endif
