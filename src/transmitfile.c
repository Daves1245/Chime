#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>

#include "connection.h"
#include "transmitfile.h"
#include "defs.h"
#include "math.h"

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
STATUS sendheader(int transferfd, const struct fileheader *fi) {
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
 */ 
STATUS recvheader(int transferfd, struct fileheader *fi) {
  char buff[HEADERBUFF_LEN], *fieldp;
  int received = 0, tmp, flag = 0;
  strtok(buff, "\n");
  while (flag < 2) {
    tmp = recv(transferfd, buff + received, HEADERBUFF_LEN - received, 0);
    if (tmp < 0 && errno != EINTR) {
      // TODO fatal()
      printf("FATAL: Could not receive file header\n");
      perror("recv");
      exit(EXIT_FAILURE); // TODO fatal?
    }
    if (tmp == 0) {
      printf("WARNING: File header not received completely\n");
      return ERROR_INCOMPLETE_RECV;
    }
    fieldp = strtok(NULL, "\n");
    if (fieldp && flag) {
      flag++;
      fi->size = atol(fieldp);
    }
    if (fieldp && !flag) {
      flag++;
      strcpy(fi->filename, fieldp);
    }
  }
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
STATUS downloadfile(int transferfd, int outfd, const struct fileheader *fi) {
  char buff[FILEBUFF_LEN];
  int received = 0, written = 0, tmp;
  while (received < fi->size) {
    int bufflen;
    tmp = recv(transferfd, buff + received, min(sizeof(buff) - received, fi->size - received), 0);
    if (tmp < 0 && errno != EINTR) {
      perror("recv");
      exit(EXIT_FAILURE); // TODO fatal?
    }
    if (tmp == 0) {
      // TODO keep file or trash it if unfinished? Keep in cache and retry upload?
      return ERROR_CONNECTION_CLOSED;
    }

    received += tmp;
    bufflen = tmp;
    while (written < bufflen) {
      tmp = write(outfd, buff + written, bufflen - written);
      if (tmp < 0 && errno != EINTR) {
        perror("write");
        exit(EXIT_FAILURE);
      }
      if (tmp == 0) {
        return ERROR_CONNECTION_CLOSED;
      }
      written += tmp;
    }
  }
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
STATUS uploadfile(int filefd, int transferfd, const struct fileheader *fi) {
  char buff[FILEBUFF_LEN];
  int sent = 0, bread = 0, tmp;
  while (bread < fi->size) {
    int bufflen;
    tmp = read(filefd, buff + bread, min(sizeof(buff) - bread, fi->size - bread));
    if (tmp < 0 && errno != EINTR) {
      // TODO make fatal() instead of manual exit
      perror("read");
      exit(EXIT_FAILURE);
    }
    if (tmp == 0) {
      return ERROR_INCOMPLETE_SEND;
    }
    bread += tmp;
    bufflen = tmp;
    while (sent < bufflen) {
      tmp = send(transferfd, buff + sent, bufflen - sent, 0);
      if (tmp < 0 && errno != EINTR) {
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
  return OK;
}
