/*
 * server.c - simple UDP server demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "common.h"

#define PORT "33404"

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &((struct sockaddr_in *) sa)->sin_addr;
  }
  return &((struct sockaddr_in6 *) sa)->sin6_addr;
}

int main(void) {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    socklen_t addr_len;
    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // set to AF_INET6 to use IPv6
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP


    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("listener: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("listener: bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(EXIT_FAILURE);
    }

    addr_len = sizeof their_addr;

    /*
       if ((numbytes = recvfrom(sockfd, buff, MAX_BUFF_LEN - 1, 0,
       (struct sockaddr *) &their_addr, &addr_len)) == -1) {
       perror("recvfrom");
       exit(1);
       }
       */

    printf("receiving (shouldn't block!)\n");
    receive_unsafe(sockfd, (struct sockaddr *) &their_addr, &addr_len);
    printf("done receiving\n");
    handle_packet();
    struct file_transfer_info *finfo = a_tofti();
    if (!finfo) {
        fprintf(stderr, "invalid finfo!\n");
        printf("received buffer was `%s`\n", buff);
        exit(EXIT_FAILURE);
    }
    print_fti_source(finfo);
    int fd;
    //sprintf(finfo->file_name, "saves/%s", finfo->file_name);
    // TODO is this undefined? check man 2 sprintf
    // if src and dest pointers overlap, then it is undefined
    // behavior. But this seems to work in practice vs
    // using `sprintf(buf, "%s text", buf);`
    char tmp[9999];
    strcpy(tmp, finfo->file_name);
    sprintf(finfo->file_name, "saves/%s", tmp);
    printf("New filename is `%s`\n", finfo->file_name);
    if ((fd = open(finfo->file_name, O_CREAT | O_WRONLY, S_IRUSR)) == -1) {
        fprintf(stderr, "Could not create file for writing\n");
        perror("open");
        exit(EXIT_FAILURE);
    }
    printf("entering loop...\n");
    while (1) {
        receive_unsafe(sockfd, (struct sockaddr *) &their_addr, &addr_len);
        handle_packet();
        struct file_transfer_packet *ftp = a_toftp();
        printf("seq number is %d\n", ftp->seq);
        printf("writing to byte %ld\n", (off_t) ftp->seq * finfo->ftp_data_size);
        printf("data being written: ||||%s||||\n", ftp->data);
        printf("length of data: %ld\n", strlen(ftp->data));
        lseek(fd, ftp->seq * finfo->ftp_data_size, SEEK_SET);
        int bwritten = 0;
        printf("Writing packet to file...\n");
        while (bwritten < finfo->ftp_data_size) {
            int tmp;
            if ((tmp = write(fd, ftp->data + bwritten, finfo->ftp_data_size - bwritten)) < 0) {
                fprintf(stderr, "error while writing\n");
                exit(EXIT_FAILURE);
            }
            bwritten += tmp;
        }
        printf("Finished.\n");
    }

    close(sockfd);
    return 0;
}
