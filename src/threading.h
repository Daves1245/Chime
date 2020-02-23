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
};

void endconnection();                               /* Terminate the connection */
void getinput(char *dest, size_t *res, size_t len); /* store '\n'-terminated line at most len into dest and modify *res accordingly */
void *thread_recv(void *);                          /* Message receiving thread */
void *thread_send(void *);                          /* Message sending thread */
void *connection_handler(void *);                   /* Connection handling thread */

/*
 * XXX - implement actual end connection protocol
 * - check for recognition of logging off
 */
void endconnection(void) {
  struct message tmp;
  memset(&tmp, 0, sizeof tmp);
  tmp.flags = FDISCONNECT;
  sendmessage(sfd, &tmp);
  close(sfd);
  printf("Disconnected\n");
  exit(EXIT_SUCCESS);
}

/*
 *the more i work on this the more i realize i'm
 just making a bad IRC clone for C.
 */
// XXX
void sighandler(int s) {
  if (s == SIGTERM) {
    endconnection();
  }
}

void *thread_recv(void *handlei) {
  struct handlerinfo *info = handlei;
  struct message msg;
  msg.txt = malloc(MAX_TEXT_LEN); /* XXX _guarantee_ this is freed */

  strcpy(msg.from, info->handle);
  msg.flags = FCONNECT;
  sendmessage(info->sfd, &msg);

  while (1) {
    recvmessage(info->sfd, &msg);
    switch (msg.flags) {
	    case FDISCONNECT:
		    printf(YELLOW "[%s left the chat]" ANSI_RESET "\n", msg.from);
		    break;
	    case FCONNECT:
		    printf(YELLOW "[%s entered the chat]" ANSI_RESET "\n", msg.from);
		    break;
	    case FMSG:
        displaymessage(&msg);
		    break;
      default:
        printf(RED "[invalid flags, defaulting to displaymsg]" ANSI_RESET "\n");
        displaymessage(&msg);
        break;
    }
  }
}

void *thread_send(void *handlei) {
	struct handlerinfo *info = handlei;
	struct message msg;
	size_t msgtxtlen;

	memset(&msg, 0, sizeof msg);
	memcpy(msg.from, info->handle, strlen(info->handle));
  msg.id = 0;
  msg.uid = 1;
  msg.txt = malloc(MAX_TEXT_LEN);
  msg.flags = FMSG;

	struct pollfd listener;
	listener.fd = 0; // poll for stdin
	listener.events = POLLIN; // wait till we have input

	while (1) {
		poll(&listener, 1, -1); // block until we can read
		if (listener.revents == POLLIN) {
			/* XXX Grab input, check for exit */
			getinput(msg.txt, &msgtxtlen, MAX_TEXT_LEN);
			if (msgtxtlen == 0) {
				memcpy(msg.txt, "/exit", 5);
			}
			msg.id++;
			sendmessage(info->sfd, &msg);
			if (strcmp(msg.txt, "/exit") == 0) {
				exit(EXIT_SUCCESS);
			}
		}
	}
}
