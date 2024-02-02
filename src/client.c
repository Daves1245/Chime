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
#include <inttypes.h>

#include <utils/defs.h>
#include <utils/colors.h>
#include <utils/functions.h>
#include <ftransfer/transmitfile.h>
#include <ftransfer/fileheader.h>
#include <ftransfer/fileinfo.h>
#include <ftransfer/ft_request.h>

#include <signaling.h>
#include <message.h>
// #include "threading.h"
#include <transmitmsg.h>
#include <logging.h>

volatile sig_atomic_t connected;

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
int client_running;

struct addrinfo *servinfo, *addrinfo_iter_p;

// XXX move src/threading.h into here 
// XXX if (messagetype == FUPLOAD) set fh accordingly
// and
//  pthread_cond_broadcast(&file_ready)
// this should let thread filetransfer upload/download the 
// necessary file and then sleep again

/*
 * server_sa_handle() - Catch SIGINT and SIGTERM and disconnect from
 * the server
 *
 * The signal handler simply sets the connected
 * flag to false.
 */
void server_sa_handle(int signal, siginfo_t *info, void *ucontext) {
    client_running = 0;
}

void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
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

    while (client_running) {
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

    while (client_running) {
        if (poll(&listener, 1, POLL_TIMEOUT) && listener.revents == POLLIN) {
            /* XXX Grab input, check for exit */
            packmessage(&msg);
            msg.id++;
            STATUS s = sendmessage(conn->sfd, &msg);
            if (s != OK || msg.flags == FDISCONNECT) {
                client_running = 0;
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
    client_running = 1;
}

/*
 * client_init(void) - initialize a client
 *
 * For now, we simply set up signal handling
 * in case the user interrupts, that way we
 * can exit gracefully
 */
void client_init(void) {
    struct sigaction s_act, s_oldact;
    int res;

    /* Signal Handling */
    s_act.sa_sigaction = server_sa_handle;
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

void *file_transfer(void *args) {
    return NULL;
}

/*
 * name: main
 *
 * Connect to the server and talk. Disconnect
 * gracefully on exit.
 */
int client_main(char *hostname, char *port, char *ft_port) {
    char serverip[INET6_ADDRSTRLEN];
    pthread_t sendertid;
    pthread_t receivertid;
    pthread_t file_transferid;

    /* Init, connect and login */
    client_init(); // TODO ERROR CHECK THIS VALUE!
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
