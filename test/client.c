#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "common.h"

#define PORT "33404"
#define FTP_DATA_SIZE 100
#define FTPA_INTERVAL 2

char filebuff[99999];

struct ftp_table {
    struct file_transfer_info *finfo;
    int *unacknowledged;
    struct file_transfer_packet **packets;
};

struct ftp_table *make_ftp_table(const char *filename) {
    struct ftp_table *ret = malloc(sizeof(struct ftp_table));
    if (!ret) {
        fprintf(stderr, "malloc()\n");
        exit(EXIT_FAILURE);
    }
    struct stat st;
    stat(filename, &st);
    ret->finfo = make_fti(filename, st.st_size, FTP_DATA_SIZE, FTPA_INTERVAL);
    if (!ret->finfo) {
        fprintf(stderr, "malloc()\n");
        exit(EXIT_FAILURE);
    }
}

void tmp_send_stdin(int socket_fd, struct addrinfo *p) {
    while (1) {
        char buff[1024];
        fgets(buff, sizeof buff, stdin);
        send_unsafe(socket_fd, buff, strlen(buff), p);
        memset(buff, 0, sizeof(buff));
    }
}

int main(int argc, char **argv) {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;

    if (argc != 3) {
        fprintf(stderr, "usage: %s [hostname] [file]\n", argv[0]);
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "gettaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for (p = servinfo; p; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("socket:");
            exit(EXIT_FAILURE);
        }
        break;
    }

    if (p == NULL) {
        // who cares?
    }

    /*
    if ((numbytes = sendto(sockfd, argv[2], strlen(argv[2]), 0, p->ai_addr, p->ai_addrlen)) == -1) {
        perror("sendto");
        exit(1);
    }

    printf("sent %d bytes to %s\n", numbytes, argv[1]);
    */

    struct stat st;
    stat(argv[2], &st);
    printf("[DEBUG]: st_size is %ld\n", st.st_size);
    struct file_transfer_info *finfo = make_fti(argv[2], st.st_size, 40, 2);
    fti_toa(finfo);
    send_unsafe(sockfd, buff, strlen(buff), p);

    FILE *fp = fopen(argv[2], "r");
    int offset = 0;
    while (!feof(fp)) {
        char localbuff[10000] = {0};
        fgets(localbuff, sizeof localbuff, fp);
        strcpy(filebuff + offset, localbuff);
        offset += strlen(localbuff);
    }
    offset = 0;
    for (int i = 0; offset < st.st_size; i++) {
        struct file_transfer_packet ftp;
        memset(&ftp, 0, sizeof(ftp));
        ftp.seq = i;
        strncpy(ftp.data, filebuff + offset, 40);
        printf("sending data: `%s`\n", ftp.data);
        printf("strlen data: %ld\n", strlen(ftp.data));
        ftp_toa(&ftp);
        send_unsafe(sockfd, buff, strlen(buff), p);
        offset += strlen(ftp.data);
    }
    //receive_unsafe(sockfd, NULL, NULL);
    close(sockfd);
    return 0;
}
