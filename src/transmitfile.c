#include "connection.h"
#include "transmitfile.h"

STATUS setup_file_upload(struct connection *conn, struct fileheader *fheader) {
  
  return OK;
}

STATUS setup_file_download(struct connection *conn, struct fileheader *fheader) {
  
  return OK;
}

STATUS uploadfile(struct connection *conn, int filefd) {
  
  return OK;
}

STATUS downloadfile(struct connection *conn, int outfd) {

  return OK;
}
