#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>

#include "connection.h"
#include "ftransfer.h"
#include "defs.h"
#include "transmitmsg.h"
#include "ftrequest.h"
#include "math.h"

// TODO use SOCK_SEQPACKET ? 

extern int upload_dir_fd;
extern int download_dir_fd;

/*
 * sendheader() - send a file header
 * @transferfd: socket file descriptor to send
 *    header through
 * @fi: fileheader to be sent
 *
 * sendheader() attemps to send a fileheader through
 * the socket referred to by transferfd. fi must not
 * be null and transferfd must be a valid file descriptor. 
 *
 * Return: 
 * * OK on success
 * * ERROR_INCOMPLETE_SEND
 *    The connection on transferfd was closed prematurely,
 *    and the full fileheader could not be sent. This is
 *    fatal to the file transfer attempt since none of the
 *    file was sent through the connection.
 *    XXX return from file transfer thread unsuccessful
 */
status sendheader(int transferfd, const struct fileheader *fi) {
  char buff[HEADERBUFF_LEN];
  int sent = 0, bufflen, tmp;
  sprintf(buff, "%s\n%ld\n", fi->filename, fi->size);
  bufflen = strlen(buff);
  while (sent < bufflen) {
    tmp = send(transferfd, buff + sent, bufflen - sent, 0);
    if (tmp < 0 && errno != EINTR) {
      // TODO fatal()
      perror("send");
      exit(EXIT_FAILURE);
    }
    if (tmp == 0) {
      printf("WARNING: File header not completely sent\n");
      return ERROR_INCOMPLETE_SEND;
    }
    sent += tmp;
  }
  return OK;
}

/*
 * recvheader() - receive a file header
 * @transferfd: socket to receive from
 * @fi - pointer to store contents of received
 *    header into 
 *
 * recvheader() attmpts to receive a full file header from
 * the connection associated with the socket file descriptor
 * transferfd. It parses received data using a newline "\n"
 * delimiter and stores the resulting content in fi.
 * transferfd must refer to a valid socket.
 * 
 * recvheader() may terminate the program if the 
 * system call recv() returns an error state other than
 * EINTR.
 *
 * Return:
 * * OK on success
 * * ERROR_INCOMPLETE_RECV
 *    the connection was closed before the data received filled
 *    every field of fi.
 *
 *  TODO this currently takes some of the file. make it so that
 *  any data not part of the header is sent back to the socket so
 *  that a later recv() may rightfully get it.
 */ 
status recvheader(int transferfd, struct fileheader *fi) {
  char buff[HEADERBUFF_LEN], *fieldp;
  int received = 0, tmp;
  memset(buff, 0, sizeof buff);
  received = recv(transferfd, buff, sizeof(buff), 0);
  printf("RECVHEADER: initial recv gave a buffer of - `%s`\n", buff);
  fieldp = strtok(buff, "\n");
  printf("RECVHEADER: initial strtok returned fieldp = `%s`\n", fieldp);
  printf("RECVHEADER: probing for first field...\n");
  while (!fieldp) {
    while ((tmp = recv(transferfd, buff + received, sizeof(buff) - received, 0)) == -1 && errno == EINTR);
    if (tmp < 0) {
      perror("recv");
      return ERROR_FAILED_SYSCALL;
    }
    if (tmp == 0) {
      return ERROR_CONNECTION_LOST;
    }

    fieldp = strtok(NULL, "\n");
  }
  printf("RECVHEADER: probing done. full filename is: `%s`\n", fieldp);
  strcpy(fi->filename, fieldp);
  fieldp = strtok(NULL, "\n");
  printf("RECVHEADER: initial strtok for size field returned: `%s`\n", fieldp);
  printf("RECVHEADER: probing for the rest of size field...\n");
  while (!fieldp) {
    while ((tmp = recv(transferfd, buff + received, sizeof(buff) - received, 0)) == -1 && errno == EINTR);
    if (tmp < 0) {
      perror("recv");
      return ERROR_FAILED_SYSCALL;
      if (tmp == 0) {
        return ERROR_CONNECTION_LOST;
      }
    }
    fieldp = strtok(NULL, "\n");
  }
  fi->size = atol(fieldp);
  printf("RECVHEADER: Final probing done. size is %ld\n", fi->size);
  printf("RECVHEADER: exiting recvheader\n");
  return OK;
}

/*
 * downloadfile() - receives and writes
 *  a file from the file descriptor
 *  transferfd and into outfd
 * @transferd: file descriptor representing
 *  the socket connection from which to download the file
 * @outfd: file descriptor representing where to write
 *  the contents of the received file into
 * @fi: a fileheader that represents the file to be received
 *
 * downloadfile() attempts to receive the file represented by
 * fi and store the contents into the file represented by outfd.
 *
 * Return:
 * * OK on success
 * * ERROR_CONNECTION_CLOSED
 *      The number of bytes received was less than that specified
 *      by fi->size. The resulting file may or may not be incomplete,
 */
status downloadfile(int transferfd, int outfd, const struct fileheader *fi) {
  printf("downloadfile called with transferfd %d and outfd %d\n", transferfd, outfd);
  char buff[FILEBUFF_LEN];
  int received = 0, written = 0, tmp;
  while (received < fi->size) {
    int bufflen;

    printf("file size: %ld, received bytes: %d\n", fi->size, received);
    tmp = recv(transferfd, buff + (received % sizeof(buff)), min(sizeof(buff) - (received % sizeof(buff)), fi->size - received), 0);
    if (tmp < 0) {
      if (errno == EINTR) continue;
      perror("recv");
      exit(EXIT_FAILURE); // TODO fatal?
    }
    received += tmp;
    bufflen = tmp;
    while (written < received) {
      tmp = write(outfd, buff + (written % sizeof(buff)), bufflen - (written % sizeof(buff)));
      if (tmp < 0) {
        if (errno == EINTR) continue;
        perror("write");
        exit(EXIT_FAILURE);
      }
      written += tmp;
    }

    if (bufflen == 0) {
      printf("leaving downloadfile. file size: %ld. bytes received: %d. bytes written: %d\n", fi->size, received, written);
      return ERROR_CONNECTION_CLOSED;
    }
  }
  printf("finished downloadfile. file size: %ld. bytes received: %d. bytes written: %d\n", fi->size, received, written);
  return OK;
}

/*
 * uploadfile() - send a file through filefd
 * @filefd: file descriptor of the file to upload
 * @transferfd: socket file descriptor to send the file through
 * @fi: a file header describing the contents of the file
 *    refered to by filefd
 *
 * uploadfile() buffers data read from filefd and sends it in chunks.
 * filefd and transferfd must refer to valid file descriptors, and
 * fi must be non null.
 *
 * Return:
 * * OK on success
 * * ERROR_INCOMPLETE_SEND
 *    the number of bytes sent was less than fi->size
 */
status uploadfile(int filefd, int transferfd, const struct fileheader *fi) {
  printf("uploadfile called with filefd: %d\n", filefd);
  char buff[FILEBUFF_LEN];
  int sent = 0, bread = 0, tmp;

  /* Read from beginning of file TODO move offset back to this afterwards */
  lseek(filefd, 0, SEEK_SET);
  while (bread < fi->size) {
    int bufflen;
    tmp = read(filefd, buff + (bread % sizeof(buff)), min(sizeof(buff) - (bread % sizeof(buff)), fi->size - bread));
    if (tmp < 0) {
      if (errno == EINTR) continue;
      // TODO make fatal() instead of manual exit
      perror("read");
      exit(EXIT_FAILURE);
    }

    bread += tmp;
    bufflen = tmp;
    while (sent < bread) {
      printf("file size: %ld, bytes sent: %d\n", fi->size, sent);
      tmp = send(transferfd, buff + (sent % sizeof(buff)), bufflen - (sent % sizeof(buff)), 0);
      if (tmp < 0) {
        if (errno == EINTR) continue;
        // TODO fatal()
        perror("send");
        exit(EXIT_FAILURE);
      }

      if (tmp == 0) {
        printf("Warning: File not completely sent");
        return ERROR_INCOMPLETE_SEND;
      }

      sent += tmp;
    }
  }
  printf("leaving uploadfile. file size: %ld, bytes read: %d. bytes sent: %d\n", fi->size, bread, sent);
  return OK;
}

status create_upload_request(struct ftrequest **dest, char *filepath) {
  struct ftrequest *request;
  struct stat file_stats;
  int fd;

  request = malloc(sizeof(struct ftrequest));
  if (!request) {
    perror("malloc");
    return ERROR_FAILED_SYSCALL; // TODO malloc is not a syscall
  }
  if ((fd = open(filepath, O_RDONLY)) == -1) {
    free(request);
    if (errno == EINTR) {
      return ERROR_INTERRUPTED;
    }
  }
  if (stat(filepath, &file_stats) < 0) {
    free(request);
    perror("stat");
    return ERROR_FAILED_SYSCALL;
  }
  /* Make sure things are valid */
  if (S_ISDIR(file_stats.st_mode)) {
    fprintf(stderr, "Cannot upload a directory\n");
    free(request);
    return ERROR_INVALID_ARGUMENTS;
  }

  /* Note this sets broadcast to false by default */
  memset(request, 0, sizeof(struct ftrequest));

  /* Pack the header */
  strcpy(request->finfo.header.filename, basename(filepath));
  request->finfo.header.size = file_stats.st_size;

  /* Pack the fileinfo struct */
  request->finfo.fd = fd;

  /* Request to upload */
  request->finfo.status = UPLOAD;

  /* Finalize changes */
  *dest = request;

  /* conn should be added oustide of this function */
  return OK;
}

void *handle_upload_request(void *connp) {
  struct connection *conn;
  struct ftrequest request;
  struct message response;

  conn = (struct connection *) connp;
  if (!conn) {
    fprintf(stderr, "[handle_upload_request]: Received invalid arguments! Aborting transfer\n");
    return NULL;
  }

  memset(&response, 0, sizeof response);
  response.uid = response.id = 0;
  strcpy(response.from, " ");
  timestampmessage(&response);
  response.flags = FMSG;

  if (recvheader(conn->transferfd, &request.finfo.header) != OK) {
    fprintf(stderr, "[handle_upload_request]: recvheader returned non-OK status\n");
  }

  // TODO check data

  printf("download_dir_fd: %d\n", download_dir_fd);
  if ((request.finfo.fd = openat(upload_dir_fd, request.finfo.header.filename, O_CREAT | O_WRONLY, MODE)) < 0) {
    printf("[handle_upload_request]: Could not open new file in downloads directory\n");
    perror("open");
  }

  strcpy(response.txt, "OK");
  sendmessage(conn->transferfd, &response);

  if (downloadfile(conn->transferfd, request.finfo.fd, &request.finfo.header) != OK) {
    fprintf(stderr, "[handle_upload_request]: downloadfile returned non-OK status\n");
  }

  return NULL;
}

void *filetransfer(void *request) {
  struct ftrequest *req = (struct ftrequest *) request;
  struct message msg;

  if (!req->conn) {
    printf("filetransfer must be called with an initialized member connection!\n");
    return NULL;
  }

  memset(&msg, 0, sizeof msg);
  msg.id = msg.uid = 0;
  strcpy(msg.from, " ");
  timestampmessage(&msg);
  msg.flags = FMSG;

  switch (req->finfo.status) {
    case UPLOAD:
      printf("BEFORE SH CONN IS %p\n", req->conn);
      printf("[filetransfer]: beginning upload\n");
      printf("[filetransfer]: sending header...\n");
      if (sendheader(req->conn->transferfd, &req->finfo.header) != OK) {
        printf("SENDHEADER RETURNED NON-OK STATUS\n");
      }
      recvmessage(req->conn->transferfd, &msg);
      printf("[filetransfer]: Response: %s\n", msg.txt);
      printf("AFTER SH CONN IS %p\n", req->conn);
      printf("[filetransfer]: done\n");
      printf("[filetransfer]: sending file...\n");
      if (uploadfile(req->finfo.fd, req->conn->transferfd, &req->finfo.header) != OK) {
        printf("UPLOADFILE RETURNED NON-OK STATUS\n");
      }
      recvmessage(req->conn->transferfd, &msg);
      printf("[filetransfer]: Response: %s\n", msg.txt);
      break;
    case DOWNLOAD:
      printf("[filetransfer]: beginning download\n");
      printf("[filetransfer]: receiving header...\n");
      if (recvheader(req->conn->transferfd, &req->finfo.header) != OK) {
        printf("RECVHEADER RETURNED NON-OK STATUS\n");
      }
      strcpy(msg.txt, "OK");
      sendmessage(req->conn->transferfd, &msg);
      printf("[filetransfer]: done\n"); 
      printf("[filetransfer]: downloading file...\n");
      if (downloadfile(req->conn->transferfd, req->finfo.fd, &req->finfo.header) != OK) {
        printf("[filetransfer]: DOWNLOADFILE RETURNED NON-OK STATUS\n");
      }
      break;
    case NOT_READY:
      fprintf(stderr, "[filetransfer]: Thread created when file not ready to be handled\n");
      return NULL;
      break;
    default:
      fprintf(stderr, "[filetransfer]: Unrecognized status option\n");
      break;
  }
  close(req->finfo.fd);
  free(request);
  return NULL;
}
