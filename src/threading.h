#include <inttypes.h>
#include <time.h>
#include <signal.h>
#include <math.h>
// XXX #ifdef __linux__
#include <sys/sendfile.h>
#include <sys/stat.h>

#include "colors.h"
#include "defs.h"
#include "connection.h"
#include "transmitmsg.h"
#include "signaling.h"

/***********
 * XXX
 * - user profiles 
 * - timestamp messages 
 * - implement message hashing
 * - chat storing (locally and server side)
 * - more user commands
 ***********/


void disconnect();                                  /* Terminate the current connection */
void getinput(char *dest, size_t *res, size_t len); /* store line of text at most len into dest and modify *res accordingly */
void *thread_recv(void *);                          /* Message receiving thread */
void *thread_send(void *);                          /* Message sending thread */
void *connection_handler(void *);                   /* Connection handling thread */

/*
 * XXX Create and implement robust disconnect protocol
 * - 
 */

