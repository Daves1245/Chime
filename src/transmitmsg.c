#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>

#include "defs.h"
#include "colors.h"
#include "user.h"
#include "status.h"
#include "types.h"
#include "message.h"
#include "transmitmsg.h"
#include "connection.h"

// TODO resolve translation unit organiztion (multiple definitions of joseph)
extern volatile sig_atomic_t connected;

/***********
 * XXX
 * - user profiles 
 * - timestamp messages 
 * - implement message hashing
 * - chat storing (locally and server side)
 * - more user commands
 ***********/

// hint - it's in another file!
extern STATUS uploadfile(struct connection *conn, int fd);

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

        }
    } else { // commands without arguments
        if (strcmp(msg->txt + 1, "exit\n") == 0) {
            msg->flags = FDISCONNECT;
        }
    }
    return OK;
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



/*
 * receive_wrapper() - wrapper for grabbing fields in recvmessage
 * @sfd: the socket file descriptor of the connected user
 * @buff: the buffer to recv into
 * @size: the size of the buffer to recv into
 * @field: pointer to string that stores the next field of the message
 *
 * A wrapper for recvmessage to clean up grabbing each field for recvmessage
 * 
 * Return: bytes read by recv
 */
size_t receive_wrapper(int sfd, void *buff, size_t size, char **field) {
    char *tmp = NULL;
    size_t len = 0;
    while (!tmp) {
        len = recv(sfd, buff, size, 0);
        if (len < 0) {
            if (errno == EINTR) {
                continue; // interrupted syscall, try again
            }
            perror("recv");
            exit(EXIT_FAILURE); // TODO should this be fatal?
        }
        if (len == 0) {
            fprintf(stderr, "Connection has closed. Cannot receive message\n");
            connected = 0;
            return 0;
        }
    }

    return len;
}

// TODO fix
/*
 * name: recvmessage
 * params: socket file descriptor sfd, message pointer msg
 *
 * Receive a message from the socket referenced by sfd and
 * store it in msg
 *
 * Return: OK on success.
 */
STATUS recvmessage(int sfd, struct message *msg) {
    char buff[UINT64_BASE10_LEN + UINT32_BASE10_LEN + MAX_TEXT_LEN + UINT32_BASE10_LEN + 4] = { 0 };
    size_t bread;
    char *tmp = NULL;
    int len = 0;
    bread = recv(sfd, buff, sizeof buff, 0);
    tmp = strtok(buff, "\n");
    while (!tmp) {
        len = recv(sfd, buff + bread, sizeof(buff) - bread, 0);  
        if (len == 0) {
            return ERROR_CONNECTION_LOST;
        }
        bread += len;
        tmp = strtok(NULL, "\n");
    }
    msg->id = atoll(tmp);
    tmp = strtok(NULL, "\n");
    while (!tmp) {
        len = recv(sfd, buff + bread, sizeof(buff) - bread, 0);  
        if (len == 0) {
            return ERROR_CONNECTION_LOST;
        }
        bread += len;
        tmp = strtok(NULL, "\n");
    }
    msg->uid = atoll(tmp);
    tmp = strtok(NULL, "\n");
    while (!tmp) {
        len = recv(sfd, buff + bread, sizeof(buff) - bread, 0);  
        if (len == 0) {
            return ERROR_CONNECTION_LOST;
        }
        bread += len;
        tmp = strtok(NULL, "\n");
    }
    strcpy(msg->from, tmp);
    tmp = strtok(NULL, "\n");
    while (!tmp) {
        len = recv(sfd, buff + bread, sizeof(buff) - bread, 0);  
        if (len == 0) {
            return ERROR_CONNECTION_LOST;
        }
        bread += len;
        tmp = strtok(NULL, "\n");
    }
    strcpy(msg->txt, tmp);
    tmp = strtok(NULL, "\n");
    while (!tmp) {
        len = recv(sfd, buff + bread, sizeof(buff) - bread, 0);  
        if (len == 0) {
            return ERROR_CONNECTION_LOST;
        }
        bread += len;
        tmp = strtok(NULL, "\n");
    }
    msg->flags = atoll(tmp);
    return OK;
}

/*
 * sendmessage() - send a message to a client
 * @sfd: the socket file descriptor to which to send the message
 * @msg: the message to be sent
 *
 * sendmessage unpacks msg into a statically allocated character buffer
 * and then sends the message in plaintext (TODO) to the client. This is 
 * done for portability and to keep things simple.
 *
 * Return: 
 * * OK - success
 * * ERROR_CONNECTION_DROPPED - sfd has closed, and a message can no longer be sent.
 *                              the client should be logged out and removed from the 
 *                              poll query.
 */
STATUS sendmessage(int sfd, const struct message *msg) {
    /* One buffer large enough to store each field - and their respective null byte */
    char buff[UINT64_BASE10_LEN + UINT32_BASE10_LEN + HANDLE_LEN + MAX_TEXT_LEN + UINT32_BASE10_LEN + 5];
    memset(buff, 0, sizeof buff);
    sprintf(buff, "%d\n%d\n%s\n%s\n%d\n%c", msg->id, msg->uid, msg->from, msg->txt, msg->flags, '\0');
    size_t sent = 0, tosend = strlen(buff);
    size_t tmp;
    while (sent < tosend) {
        tmp = send(sfd, buff, strlen(buff) + 1 - sent, 0);
        if (tmp < 0) {
            perror("send");
            exit(EXIT_FAILURE);
        } else if (tmp == 0) {
            fprintf(stderr, "Connection has closed. Cannot sent message. Exiting now...\n");
            return ERROR_CONNECTION_DROPPED;
        }
        sent += tmp;
    }
    return 0;
}

