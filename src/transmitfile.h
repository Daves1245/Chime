#ifndef TRANSMIT_FILE_H
#define TRANSMIT_FILE_H

#include "types.h"
#include "status.h"
#include "connection.h"

STATUS setup_file_upload(struct connection *conn); 
STATUS setup_file_download(struct connection *conn);
STATUS uploadfile(struct connection *conn);
STATUS downloadfile(struct connection *conn);

#endif
