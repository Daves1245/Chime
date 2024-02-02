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

#include <utils/colors.h>
#include <utils/defs.h>
#include <utils/functions.h>
#include <ftransfer/transmitfile.h>
#include <ftransfer/fileheader.h>
#include <ftransfer/fileinfo.h>
#include <ftransfer/ft_request.h>

#include "message.h"
#include "connection.h"
#include "transmitmsg.h"
#include "signaling.h"
#include "logging.h"


/*
 * v 1.0.1 TODO list
 * - cleanup
 * - uptime functionality
 */

/* Main loop variable */
volatile sig_atomic_t server_running = 1;

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
void client_sa_handle(int signal, siginfo_t *info, void *ucontext) {
    server_running = 0;
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

void *tmp_func() {
    return NULL;
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

STATUS valid_path(const char *filename) {
    struct stat sb;
    int fd;
    if (stat(filename, &sb) == 0) {
        return OK;
    }
    switch (errno) {
        case EACCES: return PERMISSION_DENIED;
        default: return FAILURE;
    }
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
    while (server_running) {
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

void server_init(void) {
    /* Signal handling */
    struct sigaction s_act, s_oldact;
    int res;

    /* Set handler for SIGINT and SIGTERM */
    s_act.sa_sigaction = client_sa_handle;
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

int num_threads;

#define BOTTLENECK 10

/*
 * name: main
 * 
 * Setup the server on the requested address,
 * and start the management threads. Gracefully
 * exit when finished.
 */
int server_main(char *port, char *ft_port) {
    /* Listen for new connections on sockfd */ 
    int sockfd;
    /* Assign newly connected client fd to newfd */
    int client_fd;
    struct sockaddr_storage their_addr; // connect's address
    socklen_t sin_size;
    char logbuff[LOGBUFF_STR_LEN];
    pthread_t managert;
    pthread_t transfert;

    log_init(stdout); // logs() will print to stdout
    server_init();

    sprintf(logbuff, "Creating server on port %s", port); // XXX logs var args
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
    while (server_running) {
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
