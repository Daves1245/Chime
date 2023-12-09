#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include "signaling.h"
#include "common.h"
#include "message.h"
#include "colors.h"
#include "defs.h"
#include "threading.h"
#include "transmitmsg.h"
#include "transmitfile.h"
#include "fileheader.h"
#include "fileinfo.h"
#include "ft_request.h"
#include "functions.h"
#include "logging.h"

/*
 * TODO
 *
 * system of error macros to make
 * error reporting easier and less error prone
 */

/* Mutexes for synchronizing file transfers */
pthread_cond_t ft_ready = PTHREAD_COND_INITIALIZER;
pthread_mutex_t ft_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Our connection with the server */
struct connection conn;

/* Currently connected to the server */
int running;

struct addrinfo *servinfo, *addrinfo_iter_p;

/* * * * *
 *
 * request queue
 *
 * * * * */

/* Queue of transfer requests */
struct ft_request *ft_request_queue = NULL;

struct ft_request *make_request(ft_status status, char *filename) {
    struct ft_request *ret = malloc(sizeof(*ret));
    ret->status = status;
    strcpy(ret->filename, filename);
    if (stat(filename, &ret->stat_sb) == -1) {
        logs(CHIME_WARN "[make_request]: Could not stat file");
        perror("stat");
        exit(EXIT_FAILURE);
    }
    push_request(ret);
}

void push_request(struct ft_request *req) {
    ft_request_queue->prev->next = req;
    req->prev = ft_request_queue->prev;
    req->next = ft_request_queue;
    ft_request_queue->prev = req;
}

struct ft_request *peek_request(void) {
    return ft_request_queue;
}

struct ft_request *pop_request(void) {
    //printf("Popped a request!\n");
    struct ft_request *ret = ft_request_queue;
    ft_request_queue = ft_request_queue->next;
    ret->prev->next = ft_request_queue;
    ft_request_queue->prev = ret->prev;
    if (ret == ret->next) {
        ft_request_queue = NULL;
    }
    return ret;
}

// TODO return 1?
int free_request(struct ft_request *req) {
    free(req);
    return 1;
}

/* Array of file descriptors of current file transfers */
/* TODO: for now, each secret will simply be the index of this
 * array. However, for security reasons, it should be some randomly
 * generated key. Ideally, this would be a hashmap 
 * UINT_64 -> fd
 */
int open_files[MAX_CONCURRENT_TRANSFERS];
int open_file_len;

int get_fd_from_secret(int secret) {
    return open_files[secret];
}

int get_offset_from_sequence(int sequence) {
    return sequence;
}

/*
 * TODO the name packmessage should 
 * really refer to the function that
 * packs a message into a character array
 * for sending over a socket.
 *
 * packmessage() - pack a message with the
 * necessary information.
 * @msg: the message to pack
 *
 * Given a message msg, packmessage fills 
 * the text, flags, and timestamp fields
 * within it.
 *
 * Return: OK on success
 */
STATUS packmessage(struct message *msg) {
    char *txt = fgets(msg->txt, MAX_TEXT_LEN + 1, stdin);
    if (!txt) {
        strcpy(msg->txt, "/exit\n");
    }
    /*
     * TODO ideally you send an empty string
     * but since we currently parse messages
     * with newline delimiters and strtok(),
     * if a packed message contains an empty
     * txt field, then two adjacent newlines
     * form a single delimiter for strtok(),
     * and the last field,flags, is not read
     * properly. So we place a space for now
     * with the intent of changing it later.
     */
    if (*msg->txt == '\n') {
        strcpy(msg->txt, " ");
    }
    if (*msg->txt == '/') {
        cmdparse(msg);
    }
    timestampmessage(msg);
    return OK;
}

// XXX move src/threading.h into here 
// XXX if (messagetype == FUPLOAD) set fh accordingly
// and
//  pthread_cond_broadcast(&file_ready)
// this should let thread filetransfer upload/download the 
// necessary file and then sleep again

// TODO expand on error states
STATUS create_request(int status, char *filename) {
    struct ft_request *ret;

    printf("Inside create_request!\n");

    /* Create the request */
    ret = malloc(sizeof(*ret));
    if (!ret) {
        logs(CHIME_WARN "[create_request]: Could not allocate memory for transfer request");
        return ERROR_NOMEM;
    }
    memset(ret, 0, sizeof(*ret));
    ret->status = status;
    strcpy(ret->filename, filename);

    if (stat(ret->filename, &ret->stat_sb) == -1) {
        free(ret);
        switch (errno) {
            case EACCES:
                printf("%s: Permission denied\n", ret->filename);
                break;
            case EFAULT:
                printf("%s: Bad address\n", ret->filename);
                break;
        }
        perror("stat");
        return ERROR_INVALID_REQUEST;
    }

    /* Add to request queue */
    if (!ft_request_queue) {
        ret->next = ret->prev = ret;
        ft_request_queue = ret;
    } else {
        ft_request_queue->next->prev = ret;
        ret->next = ft_request_queue->next;
        ft_request_queue->next = ret;
        ret->prev = ft_request_queue;
    }
    return OK;
}

/*
 * name: cmdparse
 * params: message pointer msg
 *
 * Performs the necessary action that
 * the command in msg specifies
 */
STATUS cmdparse(struct message *msg) {
    char *cmd, buff[MAX_TEXT_LEN + 1];
    strcpy(buff, msg->txt + 1);
    cmd = strtok(buff, " ");
    // cmd = strtok(NULL, " ");
    if (cmd) { // commands with arguments
        if (strcmp(cmd, "upload") == 0) {
            char *msg = strtok(NULL, " ");
            /* Remove trailing newline from fgets() */
            msg[strcspn(msg, "\n")] = 0;
            printf("Command: upload. File: '%s'\n", msg);
            pthread_mutex_lock(&ft_mutex);
            if (create_request(UPLOAD, msg) != OK) {
                logs(CHIME_WARN "Could not create upload request");
                return ERROR_INVALID_REQUEST; // TODO caution! this assumes a single argument. actually, all this code does
            }
            printf("Created file upload request\n");
            printf("Broadcasting that there is a request ready!\n");
            pthread_cond_broadcast(&ft_ready);
            pthread_mutex_unlock(&ft_mutex);
            printf("unlocked ft_request_lock\n");
            /*
               struct stat st;
               char *filepath = strtok(NULL, " ");
               int fd;
               if (!filepath) {
               fprintf(stderr, RED "usage: /upload [file]\n");
               return ERROR_INVALID_FILEPATH;
               }
               while ((fd = open(filepath, O_RDONLY)) == -1 && errno == EINTR);
               if (fd < 0 ) {
               fprintf(stderr, RED "file path must be valid");
               perror("open");
               return ERROR_INVALID_FILEPATH;
               }
               if (stat(filepath, &st) != 0) {
               fprintf(stderr, "Could not stat file\n");
               perror("stat");
               exit(EXIT_FAILURE); // XXX fatal?
               }
               */
        }
    } else { // commands without arguments
        if (strcmp(msg->txt + 1, "exit\n") == 0) {
            msg->flags = FDISCONNECT;
        }
    }
    return OK;
}

/*
 * sa_handle() - Catch SIGINT and SIGTERM and disconnect from
 * the server
 *
 * The signal handler simply sets the connected
 * flag to false.
 */
void sa_handle(int signal, siginfo_t *info, void *ucontext) {
    connected = 0;
}

/*
 * name: thread_recv
 * params: generic pointer to connection.
 *
 * The main receiving thread for the client.
 * Return value: (unused)
 */
void *thread_recv(void *pconn) {
    struct connection *conn = (struct connection *) pconn;
    struct message msg;
    STATUS s;
    memset(&msg, 0, sizeof msg);

    while (connected) {
        s = recvmessage(conn->sfd, &msg);
#ifdef DEBUG
        debugmessage(&msg);
#endif
        if (s == ERROR_CONNECTION_LOST) {
            printf(YELLOW "Connection was closed by the server" ANSI_RESET "\n");
            // XXX give a 'server closed connection' return state to thread
            return NULL;
        }
        switch (msg.flags) {
            case FDISCONNECT:
                printf(YELLOW "[%s left the chat]" ANSI_RESET "\n", msg.from);
                break;
            case FCONNECT:
                printf(YELLOW "[%s entered the chat]" ANSI_RESET "\n", msg.from);
                break;
            case FMSG:
                showmessage(&msg);
                break;
            case ECONNDROPPED:
                printf(YELLOW "Connection was closed by the server" ANSI_RESET "\n");
                break;
            default:
                printf(RED "[invalid flags, defaulting to displaymsg]" ANSI_RESET "\n");
                showmessage(&msg);
                break;
        }
    }
    return NULL;
}

/*
 * name: thread_send
 * params: generic pointer to connection
 *
 * The main send thread for clients. Handles
 * packing a message with user and text info,
 * and then sends the packed message to the server
 */
void *thread_send(void *pconn) {
    struct connection *conn = (struct connection *) pconn;
    struct message msg;

    // pack msg with user info and send to server
    memset(&msg, 0, sizeof msg);
    makemessage(&conn->uinfo, &msg);
    strcpy(msg.txt, " "); // TODO fix the 'empty text field' bug
    msg.flags = FCONNECT;
    sendmessage(conn->sfd, &msg);
    msg.flags = FMSG;

    struct pollfd listener;
    listener.fd = 0; // poll for stdin
    listener.events = POLLIN; // wait till we have input

    while (connected) {
        if (poll(&listener, 1, POLL_TIMEOUT) && listener.revents == POLLIN) {
            /* XXX Grab input, check for exit */
            packmessage(&msg);
            msg.id++;
            STATUS s = sendmessage(conn->sfd, &msg);
            if (s != OK || msg.flags == FDISCONNECT) {
                connected = 0;
            }
        }
    }
    disconnect_wrapper_and_exit(conn->sfd);
    return NULL;
}

void *file_upload(void *arg) {
    struct ft_request *req = (struct ft_request *) arg;
    struct sockaddr_storage their_addr;
    socklen_t addr_len;
    char buff[FILEBUFF_LEN];
    int received = 0, sent = 0;
    int attempts = 0;
    int fd;
    long long secret = 0, secret_len;
    long long sequence = 0;

    printf("Created file upload thread!\n");
retry:
    /* Tell the server what will be uploaded */
    memset(buff, 0, sizeof buff);
    sprintf(buff, "U%s\n%ld\n", req->filename, req->stat_sb.st_size);
    printf("`%s`", buff);

    while (sent < strlen(buff)) {
        int tmp = sendto(conn.ft_sfd, buff + sent, strlen(buff) + 1 - sent, 0, addrinfo_iter_p->ai_addr, addrinfo_iter_p->ai_addrlen);
        if (tmp <= 0) {
            perror("sendto");
            printf("[file_upload]: could not send file header!\n");
            return NULL;
        }
        sent += tmp;
    }
    sent = 0;
    memset(buff, 0, sizeof buff);

    /* Wait for ACK */
    while (received < ACK_LEN) {
        int tmp = recvfrom(conn.ft_sfd, buff, ACK_LEN, 0, (struct sockaddr *) &their_addr, &addr_len);
        if (tmp <= 0) {
            perror("recvfrom");
            fprintf(stderr, "[file_upload]: Could not receive ACK!\n");
            return NULL;
        }
        received += tmp;
    }
    received = 0;
    memset(buff, 0, sizeof buff);

    if (attempts > 4 && *buff != 'A') {
        fprintf(stderr, "[file_upload]: Timed out with server\n");
        return NULL;
    } else if (*buff != 'A') {
        attempts++;
        goto retry;
    }

    secret_len = strlen(buff + 1) + 1;
    secret = atoi(buff + 1);
    sprintf(buff, "%lld\n%lld\n", secret, sequence);

    /* Prepare the file */
    if ((fd = open(req->filename, USER_RW_MODE)) == -1) {
        perror("open");
        fprintf(stderr, "[file_upload]: Could not upload file\n");
        return NULL;
    }

    /* Divide file into sections, and send each section */
    while (sent < req->stat_sb.st_size) {
        int bread = 0;
        while (bread < min(sizeof buff, req->stat_sb.st_size)) {
            int tmp = read(fd, buff + secret_len + bread, sizeof buff);
            if (tmp < 0) {
                perror("read");
                fprintf(stderr, "[file_upload]: Could not read file\n");
                return NULL;
            }
            bread += tmp;
        }
        while (sent < bread) {
            int tmp = sendto(conn.ft_sfd, buff + sent, strlen(buff) + 1 - sent, 0, addrinfo_iter_p->ai_addr, addrinfo_iter_p->ai_addrlen);
            if (tmp <= 0) {
                perror("sendto");
                fprintf(stderr, "[file_upload]: Could not send file\n");
                return NULL;
            }
            sent += tmp;
        }
        memset(buff + secret_len, 0, sizeof(buff) - secret_len);
    }

    return NULL;
}

void *file_download(void *arg) {
    struct ft_request *req = (struct ft_request *) arg; 
    char buff[FILEBUFF_LEN];
    int fd;
    int received = 0;
    long long sequence = 0, secret;
    struct sockaddr_storage their_addr;
    socklen_t addr_len;

    printf("[file_download]: Started file download thread!\n");
    /* Prepare for download */
    memset(buff, 0, sizeof buff);

    while (running) {
        char *tmp;
        while (received < HEADER_LEN) {
            int tmp = recvfrom(conn.ft_sfd, buff, HEADER_LEN - received, 0, (struct sockaddr *) &their_addr, &addr_len);
            if (tmp <= 0) {
                perror("recvfrom");
                fprintf(stderr, "[file_download]: Fatal error\n");
            }
            received += tmp;
        }

        tmp = strtok(buff, "\n");
        if (!isnumber(tmp)) {
            break; /* Ignore invalid packet */
        }
        secret = atoi(tmp);
        if (!(fd = get_fd_from_secret(secret))) {
            break; /* Ignore invalid packet */
        }
        tmp = strtok(NULL, "\n");
        if (!isnumber(tmp)) {
            /* Ignore. The client should send if they do not receive an ACK packet */
            break;
        }
        sequence = atoi(tmp);
        int section_written = 0;
        if (lseek(fd, get_offset_from_sequence(sequence), SEEK_CUR) == -1) {
            perror("lseek");
            fprintf(stderr, "[file_download]: Likely generated invalid seek offset\n");
            return NULL;
        }
        while (section_written < strlen(buff)) {
            int tmp = write(fd, buff + section_written, strlen(buff) - section_written);
            if (tmp <= 0) {
                perror("write");
                fprintf(stderr, "[file_download]: Could not write to file\n");
            }
        }
    }

    /*
     * UDP File download:
     *   1) receive packet
     *   2) validate data (use secret)
     *   3) determine sequence number
     *   4) write to specific part in file (use lseek())
     */

    return NULL;
}

void *file_transfer(void *arg) {
    struct addrinfo hints;
    struct sockaddr_storage their_addr;
    socklen_t addr_len;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    /* Create socket to ft server */

    while (running) {
        struct ft_request *req;
        /* Wait for requests to be made */
        pthread_mutex_lock(&ft_mutex);
        while (!ft_request_queue) {
            pthread_cond_wait(&ft_ready, &ft_mutex);
        }
        do {
            req = pop_request();
        // We have at least one request ready to be processed
            void *(*thread)(void *);
            pthread_t pid;
            int sent = 0;
            int recieved = 0;
            if (req->status == UPLOAD) {
                thread = file_upload;
            } else if (req->status == DOWNLOAD) {
                thread = file_download;
            }

            if (pthread_create(&pid, NULL, thread, req) != 0) {
                logs(CHIME_WARN "Could not create thread");
                perror("pthread_create");
                exit(EXIT_FAILURE);
            }

            printf("req != req->next: %d\n", req != req->next);
        } while (ft_request_queue && free_request(req));
        //free_request(req);
        printf("test\n");
        pthread_mutex_unlock(&ft_mutex);
    }

    freeaddrinfo(servinfo);
    return NULL;
}

void chime_in(const char *hostname, const char *port, const char *ft_port, char *ip) {
    int sockfd;
    struct addrinfo hints;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = AF_UNSPEC;

    /* Connect to chat server */
    if ((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return; // TODO ERROR
    }

    printf("Attempting to connect...\n");
    // loop through all the results and connect to the first we can
    for (addrinfo_iter_p = servinfo; addrinfo_iter_p != NULL; addrinfo_iter_p = addrinfo_iter_p->ai_next) {
        if ((sockfd = socket(addrinfo_iter_p->ai_family, addrinfo_iter_p->ai_socktype, addrinfo_iter_p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }
        if (connect(sockfd, addrinfo_iter_p->ai_addr, addrinfo_iter_p->ai_addrlen) == -1) {
            close(sockfd);
            perror("connect");
            continue;
        }
        break;
    }
    if (addrinfo_iter_p == NULL) {
        fprintf(stderr, "client [chime_in]: failed to connect\n");
        return;
    }
    inet_ntop(addrinfo_iter_p->ai_family, get_in_addr((struct sockaddr *) addrinfo_iter_p->ai_addr), ip, INET6_ADDRSTRLEN); // ip must already be allocatedZ
    printf(GREEN "Connected to %s\n" ANSI_RESET, ip);
    freeaddrinfo(servinfo); // all done with this structure 
    conn.sfd = sockfd;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(hostname, ft_port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return;
    }

    for (addrinfo_iter_p = servinfo; addrinfo_iter_p != NULL; addrinfo_iter_p = addrinfo_iter_p->ai_next) {
        if ((conn.ft_sfd = socket(addrinfo_iter_p->ai_family, addrinfo_iter_p->ai_socktype, addrinfo_iter_p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }
        break;
    }

    if (addrinfo_iter_p == NULL) {
        fprintf(stderr, "client [chime_in]: could not connect to ft server\n");
        return;
    }

    printf("conn.ft_sfd is %d\n", conn.ft_sfd);
    printf("conn.sfd is %d\n", conn.sfd);
    printf("Inside chime_in, p->ai_addr is %p", addrinfo_iter_p->ai_addr);
    running = 1;
}

/*
 * chime_init(void) - initialize a client
 *
 * For now, we simply set up signal handling
 * in case the user interrupts, that way we
 * can exit gracefully
 */
void chime_init(void) {
    struct sigaction s_act, s_oldact;
    int res;

    /* Signal Handling */
    s_act.sa_sigaction = sa_handle;
    s_act.sa_flags = SA_SIGINFO;
    res = sigaction(SIGTERM, &s_act, &s_oldact);
    if (res != 0) {
        perror("sigaction");
    }
    res = sigaction(SIGINT, &s_act, &s_oldact);
    if (res != 0) {
        perror("sigaction");
    }
}

/*
 * login(void) - login to the server.
 *
 * prompt, parse, and authenticate user info.
 * For now, we simply ask a handle is provided.
 * TODO distinct user tracking system, with
 * genuine user authentication.
 *
 * This would ideally modify some client-side
 * user info, such as a struct user variable.
 */
void login(void) {
    printf("handle:");
    fgets(conn.uinfo.handle, HANDLE_LEN + 1, stdin);
}

/*
 * name: main
 *
 * Connect to the server and talk. Disconnect
 * gracefully on exit.
 */
int main(int argc, char **argv) {
    char *port = PORT, *ft_port = FT_PORT;
    char serverip[INET6_ADDRSTRLEN];
    char *hostname = LOCALHOST;
    pthread_t sendertid;
    pthread_t receivertid;
    pthread_t file_transferid;

    if (argc > 1) {
        hostname = argv[1];
    }

    if (argc > 2) {
        port = argv[2];
    }

    if (argc > 3) {
        ft_port = argv[3];
    }

    /* Init, connect and login */
    chime_init(); // TODO ERROR CHECK THIS VALUE!
    log_init(stdout); // set log output stream to stdout
    /* This has side effects of modifying struct connection conn! */
    chime_in(hostname, port, ft_port, serverip);
    login();

    printf("addr in main: %p\n", addrinfo_iter_p->ai_addr);
    if (pthread_create(&sendertid, NULL, thread_send, &conn)) {
        fprintf(stderr, "Could not create msg sender thread\n");
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&receivertid, NULL, thread_recv, &conn)) {
        fprintf(stderr, "Could not create msg receiving thread\n");
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&file_transferid, NULL, file_transfer, &conn)) {
        fprintf(stderr, "Could not create file transfering thread\n");
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }

    /* Wait for threads to terminate and exit */
    pthread_join(sendertid, NULL);
    pthread_join(receivertid, NULL);

    close(conn.sfd);
    return 0;
}

