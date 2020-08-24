#ifndef TRANSMIT_FILE_H
#define TRANSMIT_FILE_H

#include "types.h"
#include "status.h"
#include "fileheader.h"

STATUS uploadfile(int filefd, int transferfd, const struct fileheader *fi); /* send a file */
STATUS downloadfile(int transferfd, int outfd, const struct fileheader *fi); /* receive a file */
STATUS sendheader(int transferfd, const struct fileheader *fi); /* send a file header */
STATUS recvheader(int transferfd, struct fileheader *fi); /* receive a file header */

#endif
