#ifndef DEFS_H
#define DEFS_H

//#define DEBUGRECV 1

// Arbitrary
#define HEADER_LEN 4
#define MAX_RECV_LEN 100
#define MAX_SEND_LEN 100

#define HANDLE_LEN 30
#define MAX_TEXT_LEN 100
#define TIMESTAMP_LEN 20
#define FILENAME_LEN 100
#define FILEBUFF_LEN 1000 // TODO find optimization for this
#define HEADERBUFF_LEN 150 // TODO optimize

#define HASH_LEN 256 // length of hash

#define UINT64_BASE10_LEN 21 // XXX rename this atrocity
#define UINT32_BASE10_LEN 12 // XXX number of base10 digits needed to store all values of 32 bit integer

/* Default connection info */
#define PORT "33401" /* The default port for chatting */
#define FTPORT "33402" /* The default port for file transferring */
#define LOCALHOST "127.0.0.1" /* useful macro for loopback */

/* Wait for input in ms */
#define POLL_TIMEOUT 100

#define FMSG 0x0
#define FCONNECT 0x1
#define FDISCONNECT 0x2
#define FTRANSFER 0x3
#define ECONNDROPPED 0x4

/* Errors */
#define ECONNCLSD -1 

/* File info status variations */
#define NOT_READY 0
#define UPLOAD 1
#define DOWNLOAD 2

#endif
