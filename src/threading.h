#include <inttypes.h>
#include <time.h>
#include <signal.h>
#include <math.h>

#include "colors.h"
#include "defs.h"

/***********
 * XXX
 * - user profiles 
 * - timestamp messages 
 * - implement message hashing
 * - chat storing (locally and server side)
 * - more user commands
 ***********/

struct handlerinfo {
    int sfd;
    char *handle;
    struct user *usr;
};

void disconnect();                                  /* Terminate the current connection */
void getinput(char *dest, size_t *res, size_t len); /* store line of text at most len into dest and modify *res accordingly */
void *thread_recv(void *);                          /* Message receiving thread */
void *thread_send(void *);                          /* Message sending thread */
void *connection_handler(void *);                   /* Connection handling thread */

volatile sig_atomic_t connected = 1;

/*
 * XXX Create and implement robust disconnect protocol
 * - 
 */
void disconnect(int sfd) {
    struct message tmp;
    memset(&tmp, 0, sizeof tmp);
    strcpy(tmp.txt, "/exit");
    timestampmessage(&tmp);
    tmp.flags = FDISCONNECT;
    sendmessage(sfd, &tmp);
    close(sfd);
    printf("\nDisconnected\n");
    _Exit(EXIT_SUCCESS);
}

// XXX
void *thread_recv(void *handlei) {
    struct handlerinfo *info = handlei;
    struct message msg;
    memset(&msg, 0, sizeof msg);

    while (connected) {
        recvmessage(info->sfd, &msg);
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
            default:
                printf(RED "[invalid flags, defaulting to displaymsg]" ANSI_RESET "\n");
                showmessage(&msg);
                break;
        }
    }
    return NULL;
}

void *thread_send(void *handlei) {
    struct handlerinfo *info = handlei;
    struct message msg;

    // pack msg with user info and send to server
    memset(&msg, 0, sizeof msg);
    makemessage(info->usr, &msg);
    sendmessage(info->sfd, &msg);

    struct pollfd listener;
    listener.fd = 0; // poll for stdin
    listener.events = POLLIN; // wait till we have input

    while (connected) {
        if (poll(&listener, 1, POLL_TIMEOUT) && listener.revents == POLLIN) {
            /* XXX Grab input, check for exit */
            packmessage(&msg);
            msg.id++;
            if (strcmp(msg.txt, "/exit\n") == 0) { // fgets stores \n in buff
                connected = 0;
            }
            sendmessage(info->sfd, &msg);
        }
    }
    disconnect(info->sfd);
    return NULL;
}
