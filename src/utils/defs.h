#ifndef DEFS_H
#define DEFS_H

/* Message constants */
#define HEADER_LEN 4
#define MAX_RECV_LEN 100
#define MAX_FT_RECV_LEN 1000
#define MAX_SEND_LEN 100
#define MAX_TEXT_LEN 100
#define HANDLE_LEN 30
#define TIMESTAMP_LEN 20

/* Server info */
#define PORT "33401"
#define FT_PORT "33402"
#define UPTIME_STR_LEN 10
#define MAX_USERS 10

/* File IO utils */
#define USER_RW_MODE 0666

/* File constants */
#define FILENAME_LEN 100
#define FILEBUFF_LEN 1000 // TODO optimize
#define HEADERBUFF_LEN 200 // TODO optimize

/* File Transfer */
#define ACK_LEN 20
#define MAX_CONCURRENT_TRANSFERS 4 // TODO evaluate #cpus at runtime
#define FT_ATTEMPTS_TIMEOUT 5

/* Wait for input in ms */
#define POLL_TIMEOUT 100

/* Flags */
#define FMSG 0x0
#define FCONNECT 0x1
#define FDISCONNECT 0x2
#define FUPLOAD 0x3
#define ECONNDROPPED 0x4

/* Errors */
#define ECONNCLSD -1 

/* localhost */
#define LOCALHOST "127.0.0.1"

/* Numeric Constants */
#define NUMERIC_BASE 10
#define UINT64_BASE10_LEN 21 // Digits needed to fully encapsulate a 64bit number
#define UINT32_BASE10_LEN 12 // Digits needed to fully encapsulate a 32bit number

#endif
