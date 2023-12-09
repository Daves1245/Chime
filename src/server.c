#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>

#include "colors.h"
#include "common.h"
#include "message.h"
#include "defs.h"
#include "connection.h"
#include "transmitmsg.h"
#include "signaling.h"
#include "fileheader.h"
#include "transmitfile.h"
#include "functions.h"
#include "fileinfo.h"
#include "ft_request.h"
#include "logging.h"

/*
 * v 1.0.1 TODO list
 * - cleanup
 * - uptime functionality
 */

void broadcastmsg(struct message *m);           /* Broadcast msg to all users */
void broadcast(const char *msg, size_t msglen); /* To be deprecated */
void *manager(void *arg);                       /* Manager thread for connections */
STATUS fill_request(struct ft_request *res, char *source); /* Populate a request from its source data */

/* Main loop variable */
volatile sig_atomic_t running = 1;

/* Pointer to linked list of currently connected users */
struct connection *connections;
/* The uid to assign to the next connected user */
int next_uid;

/* Array of pollfd structures used to find out if a user has sent a message
 * or is ready to receive a message */
struct pollfd listener[MAX_USERS];
/* The number of currently connected users */
int numconns;

/* Signal handler to gracefully exit on SIGINT or SIGTERM */
void sa_handle(int signal, siginfo_t *info, void *ucontext) {
  running = 0;
}

int open_files[MAX_CONCURRENT_TRANSFERS];
int open_files_i;

/* File transfer socket file descriptor */
int ft_sfd;

/* * * * * * * * * * * 
 *
 * FIRST METHOD - CREATE A THREAD FOR EACH TRANSFER,
 * AND NOTIFY THEM WHEN NEW DATA HAS ARRIVED
 *
 * * * * * * * * * * */

/* Public info about the transfer of any thread */
struct transfer_info {
    pthread_t pid; /* The thread id */
    char *buff; /* The buffer */
};

/* TODO Hashmap of number to transfer_info */
struct transfer_info current_threads[MAX_CONCURRENT_TRANSFERS];

/* * * * * * * * * * *
 *
 * SECOND METHOD - MAKE A QUEUE OF ALL RECEIVED PACKETS,
 * HAVE A SINGLE "writer" THREAD GO THROUGH THEM AND WRITE TO FILE
 *
 * * * * * * * * * * */

off_t get_offset_from_sequence(long long sequence) {
    return sequence;
}

// TODO formalize this
int get_fd_from_secret(long long secret) {
    return (int) secret;
}

struct data_packet {
    long long secret, sequence;
    char data[MAX_FT_RECV_LEN + 1];
    socklen_t socklen; // TODO is this necessary?
    struct data_packet *next, *prev;
};

struct data_packet *data_queue;

void push_data(struct data_packet *p);
struct data_packet *peek_data(void);
struct data_packet *pop_data(void);
void free_data(struct data_packet *p);

/* Create and return a reference to a new data packet */
struct data_packet *make_data_packet(long long secret, long long sequence, char *datap) {
    struct data_packet *ret = malloc(sizeof(struct data_packet));
    assert(ret);
    ret->secret = secret;
    ret->sequence = sequence;
    strcpy(ret->data, datap);
    return ret;
}

/* Encapsulate the creation and pushing of a data packet into the queue */
void make_and_push_data(long long secret, long long sequence, char *datap) {
    struct data_packet *entry = make_data_packet(secret, sequence, datap);
    push_data(entry);
}

/* Push data onto the queue */
void push_data(struct data_packet *packet) {
    data_queue->prev->next = packet;
    packet->prev = data_queue->prev;
    packet->next = data_queue;
    data_queue->prev = packet;
}

/* Peek the head of the data queue */
struct data_packet *peek_data(void) {
    return data_queue;
}

/* Pop the head of the data queue */
struct data_packet *pop_data(void) {
    struct data_packet *ret = data_queue;
    data_queue = data_queue->next;    
    ret->prev->next = data_queue;
    data_queue->prev = ret->prev;
    return ret;
}

/* Destroy the data packet */
void free_data(struct data_packet *packet) {
    free(packet->data);
    free(packet);
}

/*
 * struct ft_owner - encapsulate the materials
 *      of a file transfer that a thread owns.
 */
struct ft_thread_info {
    // pthread_t pid;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    char global_buffer[MAX_FT_RECV_LEN + 1];
    struct sockaddr_storage client_addr;
    socklen_t addr_len;
};

/* Arbitrary for now -
 * ideally this contains a unique id
 * given for each file upload/download
 * the process should go like this
 * client ------ UP/DOWN REQUEST -----> server
 * client <------- UNIQUE ID ---------- server
 * client -------- UNID (part) (data) - server
 * client <------- ACK (part) --------- server
 * where (part) signifies a sequence number in at
 * least one packet sent from client to server
 * the client should send the same (part) again
 * if an ACK reply was not recieved, and give up
 * after a minute of silence. Similarly, the server
 * request for any missing (part) of the transfer request.
 */
/* TODO HEADER_LEN should be just enough to store
 * the unique identifier of the packet, the sequence number, and the
 * data inside the packet.
 */
#define FT_HEADER_LEN 20

STATUS ft_process_packet(char *source) {
    return OK;
}

// TODO this should do something!
// for packet processing:
//      sets some internal state to WAITING_FOR_FURTHER_INFORMATION
//      then, continues to wait on recvfrom until data is recieved, then
//      goes back to processing
// for fill_request:
//      simply ignore the invalid request
void *tmp_func(void) {
    return NULL; // do nothing
}

/* guarantee strtok returns non-null by handling NULL
 * case in a way that does not terminate the current thread or program */
char *checked_strtok(char *str, char *del) {
    char *ret = strtok(str, del);
    if (!ret) {
        tmp_func();
    }
    return ret;
}

int valid_path(const char *filename) {
    struct stat sb;
    int fd;
    if ((fd = open(filename, O_RDONLY)) == -1 && errno != EACCES) {
        return 0;
    } else if (stat(filename, &sb) == -1 && errno != EACCES) {
        return 0; 
    }
    return 1;
}

/*
 * int isinteger(str) - check if str is a number
 *
 * returns:
 *  true if str contains a valid, positive integer
 *  false otherwise
 */
int isinteger(const char *str) {
    for (const char *c = str; *c != '\0'; c++) {
        if (!isdigit(*c)) {
            return 0;
        }
    }
    return 1;
}

// TODO more descriptive error states
/* Populate an ft_request with the information recieved */
STATUS fill_request(struct ft_request *res, char *source) {
    char *tmp;
    tmp = checked_strtok(source, "\n");
    if (!isnumber(tmp)) {
        return ERROR_INVALID_REQUEST;
    }
    res->status = atoi(tmp);
    tmp = checked_strtok(source, NULL);
    if (!valid_path(tmp)){
        return ERROR_INVALID_REQUEST;
    }
    strcpy(res->filename, tmp);
    tmp = checked_strtok(source, NULL);
    if (!isnumber(tmp)) {
        return ERROR_INVALID_REQUEST;
    }
    // TODO should fill_request overwrite possible linked list structure? where else could this be used in the server?
    res->next = res->prev = NULL;
    return OK;
}

/* TODO Non invasive linked list or array */
void freeconnections(struct connection *c) {
    struct connection *iter;
    for (iter = connections; iter && iter->next != connections; iter = iter->next) {
        disconnect(iter);
    }
    if (iter) {
        disconnect(iter);
    }
}
/*
 * name: login_user 
 * params: urequest
 *
 * Check if provided user hints are valid
 * and if so, login the new user.
 */
STATUS login_user(struct connection *entry) {
    struct connection *iter;
    if (!connections) {
        connections = entry;
        connections->next = connections->prev = entry;
        return entry->uinfo.uid = next_uid++;
    }
    for (iter = connections; iter->next != connections; iter = iter->next) {
        printf("handle: %s\n", iter->uinfo.handle);
        // TODO if (iter->uinfo.uid == entry->uinfo.uid) {return ERROR_ALREADY_CONNECTED;}
        if (strcmp(iter->uinfo.handle, entry->uinfo.handle) == 0) {
            return ERROR_USERNAME_IN_USE;
        }
    }
    if (strcmp(iter->uinfo.handle, entry->uinfo.handle) == 0) {
        return ERROR_USERNAME_IN_USE;
    }

    connections->prev->next = entry;
    entry->prev = connections->prev;
    connections->prev = entry;
    entry->next = connections;
    return OK;
}

/*
 * TODO switch to use connection
 * instead of socket file descriptors
 * (after organization of user info)
 *
 * name: logoff_user
 * params: int sfd
 *
 * remove a user from the current list of 
 * active connections.
 * 
 * Return value: ERROR_USER_NOT_FOUND if
 * the requested user could not be found,
 * OK on success.
 */
STATUS logoff_user(int sfd) {
    struct connection *iter = connections;
    int flag = 0;
    while (!flag || iter != connections) {
        flag |= iter == connections;
        if (iter->sfd == sfd) {
            if (flag) {
                connections = connections->next;
            }
            iter->prev->next = iter->next;
            iter->next->prev = iter->prev;
            free(iter);
            return OK;
        }
        iter = iter->next;
    }
    return ERROR_USER_NOT_FOUND;
}

/*
 * name: broadcast
 * params: string msg, size msglen
 *
 * (DEPRECATED) broadcast a string to all users
 */
void broadcast(const char *msg, size_t msglen) {
    size_t numbytes;
    for (int i = 0; i < numconns; i++) {
        if (listener[i].fd > 0) {
            if ((numbytes = send(listener[i].fd, msg, msglen, 0)) != msglen) {
                if (numbytes < 0) {
                    perror("send");
                    break;
                } else {
                    perror("incomplete send");
                    break;
                }
            }
        }
    }
}

/*
 * name: broadcastmsg
 * params: struct message pointer
 *
 * Broadcast a message to all clients currently connected
 */
void broadcastmsg(struct message *m) {
    char logbuff[LOGBUFF_STR_LEN + MAX_TEXT_LEN];
    sprintf(logbuff, BLUE "BROADCAST (" ANSI_RESET GREEN "%s" ANSI_RESET BLUE "): " ANSI_RESET "%s", m->from, m->txt);
    logs(logbuff);
    for (int i = 0; i < numconns; i++) {
        if (listener[i].fd > 0) {
            sendmessage(listener[i].fd, m);
        }
    }
}

/*
   void *file_transfer_old(int sfd, void *finfop) {
   struct fileinfo *finfo = (struct fileinfo *) finfop;

   if (!finfo) {
   logs(CHIME_WARN "Invalid file transfer request. Aborting...\n");
   return NULL;
   } 

   switch (finfo->status) {
   case UPLOAD:
   if (sendheader(sfd, &finfo->header) != OK) {
   logs(CHIME_WARN "sendheader returned non-OK status");
   }
   if (uploadfile(sfd, finfo->fd, &finfo->header) != OK) {
   logs(CHIME_WARN "uploadfile returned non-OK status");
   } 
   logs(CHIME_INFO "finished upload procedure");
   break;
   case DOWNLOAD:
   if (recvheader(sfd, &finfo->header) != OK) {
   logs(CHIME_WARN "recvheader returned non-OK status");
   }
   if (downloadfile(sfd, finfo->fd, &finfo->header) != OK) {
   logs(CHIME_WARN "downloadfile returned non-OK status");
   }
   logs(CHIME_INFO "finished file download procedure");
   break;
   case NOT_READY:
   logs(CHIME_WARN "file_transfer called with NOT_READY file status. Exiting");
   default:
   logs(CHIME_WARN "file_transfer called with invalid transfer status");
   break;
   }
   logs(CHIME_INFO "file_transfer thread exiting");
   return NULL;
   }
   */

/* PROTOTYPE FILE MANAGEMENT */
/*
void *transfermanager(void *arg) {
    int listenfd, newfd, rv;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    int yes = 1;
    int running = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, FT_PORT, &hints, &servinfo)) != 0) {
        logs(CHIME_WARN "Could not set up file transfering thread\n");
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return NULL; // TODO proper return value
    }

    for (p = servinfo; p; p = p->ai_next) {
        if ((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }

        if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            logs(CHIME_WARN "Could not set file transfer socket to REUSE_ADDR. File transfer may be unavailable for some time");
            perror("setsockopt");
        }

        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == -1) {
#ifdef  WNO_ERROR
            if (errno == EADDRINUSE) {
                logs(CHIME_WARN "Since WNO_ERROR is set, sleeping until port is open");
                while (bind(listenfd, p->ai_addr, p->ai_addrlen) == -1 && errno == EADDRINUSE) {
                    usleep(1000 * 1000); // sleep for 1 second
                }
            }
            goto setup_check;
#endif
            close(listenfd);
            perror("bind");
            continue;
        }
        break;
    }

setup_check:
    if (!p) {
        logs(CHIME_WARN "Unable to setup file transfer server");
        logs(CHIME_WARN "Proceeding to transfer files through chat messages");
        logs(CHIME_WARN "Warning: Uploading large files may increase latency");
        logs(CHIME_WARN "Set flag NO_TRANSFER_ON_MSG to disable this feature");
        // XXX do above
    }

    if (listen(listenfd, MAX_USERS) == -1) {
        logs(CHIME_WARN "Could not set up socket for listening");
        perror("listen");
        return NULL;
    }

    logs(CHIME_INFO "File transfer setup successful");
    while (running) {
        newfd = accept(listenfd, (struct sockaddr *) &their_addr, &sin_size);
        if (newfd < 0) {
            if (errno != EINTR) {
                logs(CHIME_WARN "Warning: Error occured while listening for file transfer connections");
            }
            break;
        }

#ifdef DEBUG
        printf("%p %d\n", transfer_conns, num_transfer_conns);
#endif
        transfer_conns[num_transfer_conns] = newfd;
        num_transfer_conns++;
    }
    return NULL;
}
*/

/*
 * name: manager TODO
 * params: arg (not used)
 *
 * Manages reading and sending messages sent by clients.
 * Any unread messages are read, interpreted, and their associated
 * action performed (normally broadcasting it to the other users).
 *
 * Return value: (none; not used yet) TODO
 */
void *manager(void *arg) {
    char buff[MAX_RECV_LEN + 1];
    struct message m;
    while (running) {
        if (poll(listener, numconns, POLL_TIMEOUT)) {
            for (int i = 0; i < numconns; i++) {
                char logbuff[LOGBUFF_STR_LEN + MAX_TEXT_LEN];
                if (listener[i].fd > 0 && listener[i].revents == POLLIN) {
                    recvmessage(listener[i].fd, &m);
                    /* Connect a new user */
                    if (m.flags == FCONNECT) {
                        struct message ret;
                        struct connection *conn;

                        conn = malloc(sizeof(struct connection));
                        if (!conn) {
                            fprintf(stderr, "Could not allocate memory for user connection\n");
                            /* XXX disconnect all users here */
                            _Exit(EXIT_FAILURE);
                        }

                        memset(conn, 0, sizeof(*conn));
                        memset(&ret, 0, sizeof ret);
                        strcpy(ret.from, "SERVER");
                        timestampmessage(&ret);

                        conn->sfd = listener[i].fd;
                        conn->next = conn->prev = conn;
                        strcpy(conn->uinfo.handle, m.from);
                        STATUS s = login_user(conn);
                        if (s == ERROR_USERNAME_IN_USE) {
                            sprintf(ret.txt, "A user with the name %s already exists", conn->uinfo.handle);
                            ret.flags = ECONNDROPPED;
                            sendmessage(conn->sfd, &ret);
                            /*
                             * TODO when we organize user info later, we will be able to cleanly
                             * ask the client for a new username, instead of forcing them to reconnect.
                             * but this works for now
                             */
                            // hint that logoff_user will return more than two states later
                            STATUS s = logoff_user(listener[i].fd);
                            if (s == ERROR_USER_NOT_FOUND) {
                                logs("ERR USER NOT FOUND. This should not display. If you see this please post to https://www.github.com/Daves1245/Chime/issues");
                            }
                            continue;
                        }
                    }

                    /* 
                     * TODO fix this
                     * Disconnect a user */
                    if (strcmp(m.txt, "/exit") == 0) {
                        logoff_user(listener[i].fd);
                        listener[i].fd = -1; // remove from poll() query
                        sprintf(logbuff, "user %s disconnected", m.from);
                        logs(logbuff);
                        disconnect_wrapper(listener[i].fd);
                    }
                    broadcastmsg(&m);
                    memset(buff, 0, sizeof buff);
                }
            }
        } else {
            continue;
        }
    }
    return NULL;
}

void chime_init(void) {
    /* Signal handling */
    struct sigaction s_act, s_oldact;
    int res;

    /* Set handler for SIGINT and SIGTERM */
    s_act.sa_sigaction = sa_handle;
    s_act.sa_flags = SA_SIGINFO;
    res = sigaction(SIGTERM, &s_act, &s_oldact);
    if (res != 0) {
        perror("sigaction:");
    }
    res = sigaction(SIGINT, &s_act, &s_oldact);
    if (res != 0) {
        perror("sigaction:");
    }
    // TODO since moving this from main, ctrl^C in main causes clients to hang
    // with "connection closed by server" message.
}

int chime_bind(char *port) {
    int sockfd;
    struct addrinfo hints, *servinfo, *addrinfo_p;
    int yes = 1;
    int rv;
    char logbuff[LOGBUFF_STR_LEN] = {0};

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    } 

    /* Bind to the first valid entry in the addrinfo linked list made in getaddrinfo() */
    for (addrinfo_p = servinfo; addrinfo_p != NULL; addrinfo_p = addrinfo_p->ai_next) {
        if ((sockfd = socket(addrinfo_p->ai_family, addrinfo_p->ai_socktype, addrinfo_p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        /* This explicitly breaks TCP protocol. For now, we want to be able to restart
         * the program quickly, not having to wait for the TIME_WAIT to finish.
         */
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
#ifdef WNO_ERROR
            sprintf(logbuff, CHIME_WARN " Could not set socket option for reusable address. Latency may be experienced after rebooting server as the TCP Protocol requires the socket"
                    "wait until TIME_WAIT completes to accept new connections"); // TODO fact check this, memory is likely off 
            logs(logbuff);
#else
            exit(EXIT_FAILURE);
#endif
        }

        if (bind(sockfd, addrinfo_p->ai_addr, addrinfo_p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }

    logs(GREEN "server setup success" ANSI_RESET);
    freeaddrinfo(servinfo); // all done with this structure

    if (addrinfo_p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(EXIT_FAILURE); // There is no server if we cannot bind to a port
    }

    if (listen(sockfd, MAX_USERS) == -1) {
        perror("listen");
#ifdef WNO_ERROR // TODO double check this is correct
        if (errno == EADDRINUSE) {
            logs(CHIME_WARN "Port currently in use. Waiting until it is free... (ctrl^C to exit)");
            // TODO spinning ribbon loading screen (https://codereview.stackexchange.com/questions/139440/loading-animating-dots-in-c)
            while (listen(sockfd, MAX_USERS) == -1) {
                usleep(1000 * 1000); // wait for 1 sec before retrying
            }
        }
#else
        exit(EXIT_FAILURE);
#endif
    }
    return sockfd;
}

void *chime_send_file(void *arg) {
    return NULL;
}

pthread_t secret_owner[MAX_CONCURRENT_TRANSFERS];
int secret_owner_i = 0;

// TODO guarantee no segfaults
void set_secret_owner(long long secret, pthread_t owner) {
    secret_owner[(int) secret] = owner;
}

pthread_t get_owner_from_secret(long long secret) {
    return secret_owner[(int) secret];
}

/*
void *chime_receive_file(void *arg) {
    struct data_packet *header = (struct data_packet *) arg;
    int fd;
    char *tmp = strtok(header->data, "\n");
    assert(*tmp == 'U');
    tmp = strtok(NULL, "\n"); // filename
    if ((fd = open(tmp, USER_RW_MODE)) == -1) {
        logs(CHIME_WARN "Could not open file");
        perror("write");
        return NULL;
    }
    set_secret_owner(fd, pthread_self());
    header->secret = (long long) fd; // TODO remove this eventually - there should exist some non-identity mapping secret |-> (transfer info?) -> fd
    open_files[open_files_i++] = fd;
    memset(header->data, 0, sizeof(header->data));
    sprintf(header->data, "%lld\n%lld", header->secret, 0LL);

    int attempts = 0;
    while (attempts < FT_ATTEMPTS_TIMEOUT) {
        int sent = 0;
        while (sent < HEADER_LEN) {
            int tmp = sendto(ft_sfd, header->data + sent, HEADER_LEN - sent, 0, header->client_addr, &header->addr_len);
            if (tmp < 0) {
                logs(CHIME_WARN "[chime_receive_file]: Could not sendto handshake packet");
                perror("sendto");
                return NULL;
            }
            sent += tmp;
        }
    }

    return NULL;
}
*/

int num_threads;

struct ack_buffer {
    struct sockaddr_storage client_addr; /* Address to send ACK to */
    long long *arr;                      /* The list of packet id's (sequence numbers) to ACk */
    size_t len;                          /* The current size of the sequence vector */
};

struct ack_buffer ack_list[MAX_CONCURRENT_TRANSFERS];

void ack(struct sockaddr_storage client_addr, long long secret, long long sequence) {
    // TODO this requires knowledge of the client's port - should it be given
    // a data packet reference instead?
}

#define BOTTLENECK 10

/*
 * void acknowledge(secret, sequence) - Acknowledge a processed data packet.
 *
 *   Acknowledge that a data packet was received, and written to the appropriate
 *   file. Implements a bottleneck that does not actually send the client an ACK
 *   until BOTTLENECK number of acknowledgements have been processed, or a FIN
 *   packet received.
 *
 *  TODO to save on space, we could probably make a vector of data packets,
 *  so that we don't have to duplicate the secret and sequence numbers on separate
 *  arrays. This would also be cleaner, and more elegant.
 */
void acknowledge(struct data_packet *packet) {
    if (ack_list[packet->secret].len >= BOTTLENECK) {
        for (int i = 0; i < BOTTLENECK; i++) {
            ack(ack_list[packet->secret].client_addr, packet->secret, packet->sequence);
        }
        ack_list[packet->secret].len = 0;
    }
    *ack_list[packet->secret].arr = packet->sequence;
    ack_list[packet->secret].len++;
}

/* 
 * void *filewriter(arg) - File writing thread.
 *
 *   Goes through each received data packet, figures 
 *   out the respective file, and writes all received 
 *   data to it.
 */
void *filewriter(void *arg) {
    struct data_packet *cur;
    int fd;
    unsigned bytes_received;
    unsigned written = 0;
    while (running) {
        while ((cur = pop_data()) != NULL) {
            fd = get_fd_from_secret(cur->secret);
            bytes_received = strlen(cur->data);
            while (written < bytes_received) {
                int tmp;
                if (lseek(fd, get_offset_from_sequence(cur->sequence), SEEK_SET) == -1) {
                    logs("[filewriter]: Could not seek to proper location in file");
                    perror("lseek");
                    exit(EXIT_FAILURE);
                }
                if ((tmp = write(fd, cur->data + written, bytes_received - written)) < 0) {
                    logs(CHIME_WARN "[filewriter]: Could not write to file");
                    perror("write");
                    exit(EXIT_FAILURE);
                }
                written += tmp;
            }
            acknowledge(cur);
        }
        /* TODO Sleep until new packet arrives */
    }
    return NULL;
}

void finish_transfer(long long secret) {
    /* TODO is this really everything? */
    close(get_fd_from_secret(secret));
}

// TODO cleanup
/*
void *file_transfer(void *arg) {
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr;
    char buff[MAX_FT_RECV_LEN + 1] = {0};
    char logbuff[MAX_FT_RECV_LEN + LOGBUFF_STR_LEN + 2] = {0};
    int rv;
    socklen_t addr_len;
    int running = 1;

    // TODO modularize - this code has been seen many times before
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, FT_PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return NULL; // TODO return NO_SUCCESS
    }

    for (p = servinfo; p; p = p->ai_next) {
        if ((ft_sfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server [filetransfer]: socket");
            continue;
        }

        if (bind(ft_sfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(ft_sfd);
            perror("server [filetransfer]: bind");
            continue;
        }

        break;
    }

    if (!p) {
        fprintf(stderr, "server: [filetransfer]: failed to bind\n");
        return NULL; // TODO return NO_SUCCESS
    }

    addr_len = sizeof their_addr;
    while (running) {
        int recieved = 0;
        while (recieved < FT_HEADER_LEN) {
            int tmp = recvfrom(ft_sfd, buff, MAX_FT_RECV_LEN - 1, 0,
                    (struct sockaddr *) &their_addr, &addr_len);
            if (tmp < 0) {
                perror("recvfrom");
                // TODO handle recvfrom error
            }
            *
             * A packet with an alpha first letter is treated as a
             * transfer request. Valid requests either start with 'U'
             * for upload or 'D' for download. Any other value is
             * considered invalid.
             * A packet starting with a number will be considered a data packet,
             * which will be layed out as follows:
             * secret | sequence number | data
             * where | is the newline '\n' delimeter. If sequence is nonpositive,
             * it will be treated as a FIN packet in which case the transfer process
             * for the file associated with <secret> will be completed.
             
            if (isalpha(*buff)) {
                void *(*thread)(void *);
                int fd;
                char *filename;
                pthread_t pid;

                strtok(buff, "\n");
                filename = strtok(NULL, "\n");

                if (num_threads >= MAX_THREADS) {
                    continue; 
                }
                if (*buff == 'U') {
                    thread = chime_receive_file;
                    if ((fd = open(filename, O_CREAT | USER_RW_MODE)) == -1) {
                        switch (errno) {
                            case EACCES:
                                break;
                            default:
                                fprintf(stderr, "[%s]: Could not open file\n", __FUNCTION__);
                                perror("open");
                                break;
                        }
                    }
                } else if (*buff == 'D') {
                    printf("[%s]: client requested to download file `%s`\n", __FUNCTION__, filename);
                    if ((fd = open(filename, O_RDONLY)) == -1) {
                        switch (errno) {
                            case EACCES:
                                break;
                            default:
                                fprintf(stderr, "[%s]: Could not open file\n", __FUNCTION__);
                                perror("open");
                                break;
                        }
                    }
                    thread = chime_send_file;
                } else {
                    break;
                }

                open_files[open_files_i] = fd;
                open_files_i++;

                if (pthread_create(&pid, NULL, thread, buff)) {
                    logs(CHIME_WARN "[file_transfer]: Unable to handle client request");
                    fprintf(stderr, "pthread error\n");
                    perror("pthread");
                    continue;
                }
            } else if (isnumber(buff)) {
                long long secret, seq;
                char *sec_end, *seq_end;
                errno = 0;
                secret = strtoll(buff, &sec_end, NUMERIC_BASE);
                assert(errno == 0);
                errno = 0;
                seq = strtoll(buff, &seq_end, NUMERIC_BASE);
                if (seq == -1) {
                    acknowledge(make_data_packet(secret, seq, seq_end + 1));
                    finish_transfer(secret);
                }
                assert(errno == 0);
                make_and_push_data(secret, seq, seq_end + 1);
            }
        }
        perror("recvfrom");
        exit(1);
    }
    return NULL;
}
*/

/*
 * name: main
 * 
 * Setup the server on the requested address,
 * and start the management threads. Gracefully
 * exit when finished.
 */
int main(int argc, char **argv) {
    /* Listen for new connections on sockfd */ 
    int sockfd;
    /* Assign newly connected client fd to newfd */
    int client_fd;
    struct sockaddr_storage their_addr; // connect's address
    socklen_t sin_size;
    char *port = PORT;
    char logbuff[LOGBUFF_STR_LEN];
    pthread_t managert;
    pthread_t transfert;

    if (argc >= 2) {
        port = argv[2];
    }

    log_init(stdout); // logs() will print to stdout
    chime_init();

    sprintf(logbuff, "Creating server on port %s", PORT); // XXX logs var args
    logs(logbuff);

    sockfd = chime_bind(port);

    /* Manager will handle message transfer between clients */
    if (pthread_create(&managert, NULL, manager, NULL)) {
        logs(CHIME_FATAL "FATAL: Unable to create manager thread");
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }

    logs(GREEN "Manager thread created" ANSI_RESET);
    logs("Waiting for connections...");

    /* Accept new connections to the server */
    while (running) {
        sin_size = sizeof their_addr;
        client_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);

        if (client_fd == -1) {
            if (errno == EINTR) {
                logs("Shutting down");
                break; /* Running should be set to 0 */
            }
            perror("accept");
            continue;
        }
        /* TODO bug 1
         * require a connected client to immediately login (give user info for now)
         * or be disconnected. Because the server could be put into a state of indefinitely
         * waiting for a single client before handling new ones, it might be better to send
         * this to a (third?) thread.
         * then we can start organizing all the connected clients to make later features more
         * easy to deal with.
         * */
        listener[numconns].fd = client_fd;
        listener[numconns].events = POLLIN;
        numconns++;
    }

    /* Wait for managing thread to finish */
    pthread_join(managert, NULL);

    // We do not wait for every file transfer to finish, as large files may hang the server

    /* Disconnect and exit cleanly */
    logs("Disconnecting all users...");
    freeconnections(connections);
    logs("Done.");
    exit(EXIT_SUCCESS);
}

