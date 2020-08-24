#ifndef FILEINFO_H
#define FILEINFO_H

/* Should be modified only when ready to send */
struct fileinfo {
  int status; /* ready to upload/download file */
  int fd;
  struct fileheader header;
};

#endif
