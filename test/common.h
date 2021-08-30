#include <string.h>
#include <assert.h>

#define MAX_DATA_SIZE 1000
#define MAX_BUFF_LEN 2000

#define CHECK(s) do {\
    if (!s) { \
        fprintf(stderr, "strtok() returned NULL!\n"); \
    } \
} while (0);

#define CHECK_MEMBER_LENGTH(strp) do { \
    if (strlen((strp)) == 0) {\
        fprintf(stderr, "CHECK_MEMBER_LENGTH failed!\n"); \
        return NULL; \
    } \
} while (0);

#define CHECK_MEMBER_NUMERIC(ret, strp) do { \
    errno = 0; \
    (ret) = (int) strtol((strp), NULL, 10); \
    if (errno == EINVAL || errno == ERANGE) {\
        fprintf(stderr, "Invalid numeric member!\n"); \
        fprintf(stderr, "On exit, buffer was: %s", buff); \
        fprintf(stderr, "strp was `%s`\n", (strp)); \
        exit(EXIT_FAILURE); \
    } \
} while (0);

#define NEXT_MEMBER(membrp) do { \
    while (*(++membrp) != '\n'); \
} while (0);

#define POP_MEMBER(memberp) do { \
    while (*(memberp++) != '\n'); \
} while (0);

#define __POP_MEMBER(memberptr) do { \
    while (*(memberptr + 1) != '\n') {(memberptr)++;} \
    *(memberptr) = '\0'; \
    (memberptr)++; \
} while (0);

#define RETURN_IF_EQUAL(str, TYPE) do { \
    if (strncmp(buff, (str), strlen((str))) == 0) { \
        return (TYPE); \
    } \
} while (0);

#define DELIMITER '\n'

struct file_transfer_info {
    char file_name[256];
    int file_size;
    int ftp_data_size;
    int ftp_ack_interval;
};

struct file_transfer_packet {
    int seq;
    char data[MAX_DATA_SIZE];
};

struct ftpa {
    int lower_bound;
    int upper_bound;
};

char buff[MAX_BUFF_LEN];

enum PACKET_TYPE {
    UNKNOWN,
    FTI,
    FTP,
    FTP_ACK
};

struct file_transfer_info *make_fti(const char *file_name, int file_size, int ftp_data_size, int ftp_ack_interval);
struct file_transfer_packet *a_toftp(void);
struct file_transfer_info *a_tofti(void);
struct ftpa *a_toftpa(void);
void ftpa_toa(struct ftpa *ftpa);
void ftp_toa(struct file_transfer_packet *ftp);
void fti_toa(struct file_transfer_info *fti);
void send_unsafe(int socket_fd, void *data, size_t data_len, struct addrinfo *p);
void receive_unsafe(int socket_fd, struct sockaddr *their_addr, socklen_t *addr_len);
void print_fti(void);
void print_fti_source(struct file_transfer_info *finfo);
void print_ftp(void);
void print_ftp_source(struct file_transfer_packet *fp);
void print_ftpa(void);
void print_ftpa_source(struct ftpa *ftpa);
void handle_packet(void);
enum PACKET_TYPE get_type(void);

void print_fti(void) {
    struct file_transfer_info *tmp = a_tofti();
    print_fti_source(tmp);
    free(tmp);
}

void print_fti_source(struct file_transfer_info *finfo) {
    printf("filename: `%s`\n", finfo->file_name);
    printf("filesize: %d\n", finfo->file_size);
    printf("datasize: %d\n", finfo->ftp_data_size);
    printf("ftp ack_interval: %d\n", finfo->ftp_ack_interval);
}

void handle_packet(void) {
    enum PACKET_TYPE type = get_type();
    switch (type) {
        case FTI:
            puts("handle_packet(): received fti");
            break;
            // file_handle_request();
            break;
        case FTP:
            puts("handle_packet(): received ftp");
            break;
        case FTP_ACK:
            puts("handle_packet(): received ftp ACK");
            break;
        case UNKNOWN:
            fprintf(stderr, "get_type() could not determine the type\n");
            exit(EXIT_FAILURE);
    }
}

enum PACKET_TYPE get_type(void) {
    RETURN_IF_EQUAL("FTI", FTI);
    RETURN_IF_EQUAL("FTPA", FTP_ACK);
    RETURN_IF_EQUAL("FTP", FTP);
    return UNKNOWN;
}

struct file_transfer_packet *get_ftp(void) {
    return a_toftp();
}

struct file_transfer_info *get_fti(void) {
    return a_tofti();
}

void send_unsafe(int socket_fd, void *data, size_t data_len, struct addrinfo *p) {
    int bytes_sent;
    if ((bytes_sent = sendto(socket_fd, data, data_len, 0, p->ai_addr, p->ai_addrlen)) == -1) {
        perror("sendto");
        exit(EXIT_FAILURE);
    }
    printf("sent %d bytes\n", bytes_sent);
}

void ftpa_toa(struct ftpa *ftpa) {
    sprintf(buff, "FTPA%d\n%d\n", ftpa->lower_bound, ftpa->upper_bound);
}

void ftp_toa(struct file_transfer_packet *ftp) {
    sprintf(buff, "FTP%d\n%s", ftp->seq, ftp->data);
}

struct file_transfer_info *make_fti(const char *file_name, int file_size, int ftp_data_size, int ftp_ack_interval) {
    struct file_transfer_info *ret = malloc(sizeof(struct file_transfer_info));
    strcpy(ret->file_name, file_name);
    ret->file_size = file_size;
    ret->ftp_data_size = ftp_data_size;
    ret->ftp_ack_interval = ftp_ack_interval;
    return ret;
}

void fti_toa(struct file_transfer_info *fti) {
    sprintf(buff, "FTI\n%s\n%d\n%d\n%d\n", fti->file_name, fti->file_size, fti->ftp_data_size, fti->ftp_ack_interval);
}

struct file_transfer_packet *a_toftp(void) {
    struct file_transfer_packet *ret = malloc(sizeof(struct file_transfer_packet));
    const char *seqstr, *data;
    memset(ret, 0, sizeof(*ret));
    if (!ret) {
        fprintf(stderr, "malloc() failed\n");
        exit(EXIT_FAILURE);
    }
    // ignore magic
    assert(strncmp(buff, "FTP", 3) == 0);
    errno = 0;
    seqstr = strtok(buff + 3, "\n");
    printf("Sequence str: `%s`\n", seqstr);
    assert(seqstr);
    ret->seq = (int) strtol(seqstr, NULL, 10);
    assert(errno == 0);
    data = strtok(NULL, "\n");
    strncpy(ret->data, data, strlen(data));
    return ret;
}

int valid_filename(const char *str) {
    // TODO
    return str != NULL;
    //return 1;
}

struct file_transfer_info *a_tofti(void) {
    struct file_transfer_info *ret = malloc(sizeof(struct file_transfer_info));
    char *iter = buff;
    // ignore magic 
    NEXT_MEMBER(iter);
    char *tmp = strtok(buff, "\n");
    printf("parsing identifier\n");
    printf("popped `%s`\n", tmp);
    printf("buff is `%s`\n", buff);
    assert(strcmp(tmp, "FTI") == 0);
    tmp = strtok(NULL, "\n");
    assert(tmp);
    strcpy(ret->file_name, tmp);
    printf("parsing filename\n");
    printf("popped `%s`\n", tmp);
    printf("buff is `%s`\n", buff);
    assert(valid_filename(tmp));
    //puts("filename");
    //hacky solution. TODO FIX!!!!
    assert(ret->file_name);
    printf("parsing file size\n");
    tmp = strtok(NULL, "\n");
    assert(tmp);
    printf("popped `%s`\n", tmp);
    printf("buff is `%s`\n", buff);
    //puts("filesize");
    errno = 0;
    ret->file_size = (int) strtol(tmp, NULL, 10);
    assert(errno == 0);
    printf("parsing data size\n");
    tmp = strtok(NULL, "\n");
    printf("poppsed `%s`\n", tmp);
    printf("buff is `%s`\n", buff);
    errno = 0;
    ret->ftp_data_size = (int) strtol(tmp, NULL, 10);
    assert(errno == 0);
    printf("set data_size = %d\n", ret->ftp_data_size);
    //puts("data size");
    tmp = strtok(NULL, "\n");
    printf("parsing ftp_ack_interval\n");
    printf("poppsed `%s`\n", tmp);
    printf("buff is `%s`\n", buff);
    errno = 0;
    ret->ftp_ack_interval = (int) strtol(tmp, NULL, 10);
    assert(errno == 0);
    printf("set ack interval to %d\n", ret->ftp_ack_interval);
    return ret;
}

void receive_unsafe(int socket_fd, struct sockaddr *their_addr, socklen_t *addr_len) {
    int bytes_received;
    int tmp;
    printf("DEBUG: before receive!\n");
    while ((tmp = recvfrom(socket_fd, buff, MAX_BUFF_LEN - 1, 0, their_addr, addr_len)) > 0) {
        printf("DEBUG: inside receive!\n");
        bytes_received += tmp;
        printf("errno is %d\n", errno);
    }
    if (errno != EWOULDBLOCK) {
        perror("recvfrom");
        exit(1);
    } else {
        printf("would have blocked!\n");
    }
    printf("received %d bytes\n", bytes_received);
    printf("got packet `%s`\n", buff);
}

