#ifndef TRANSMIT_FILE_H
#define TRANSMIT_FILE_H

#include "types.h"
#include "status.h"
#include "fileheader.h"
#include "ftrequest.h"

status uploadfile(int filefd, int transferfd, const struct fileheader *fi); /* send a file */
status downloadfile(int transferfd, int outfd, const struct fileheader *fi); /* receive a file */
status sendheader(int transferfd, const struct fileheader *fi); /* send a file header */
status recvheader(int transferfd, struct fileheader *fi); /* receive a file header */
status create_upload_request(struct ftrequest **dest, char *filepath); /* create an upload request */
void *handle_upload_request(void *); /* Handle an upload request */
void *handle_download_request(void *); /* Handle a download request */
void *filetransfer(void *); /* execute a transfer request */

#endif
