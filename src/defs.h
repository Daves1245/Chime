#ifndef DEFS_H
#define DEFS_H

//#define DEBUGRECV 1

// Arbitrary
#define HEADER_LEN 4
#define MAX_RECV_LEN 100
#define MAX_SEND_LEN 100

#define MAX_PACKET_PAYLOAD_SIZE 1400
#define MAX_PACKETS_PER_MSG 256 // handle input up to 358400 bytes long

#define HANDLE_LEN 30
#define MAX_TEXT_LEN 100

#define HASH_LEN 256 // length of hash

#define UINT64_BASE10_LEN 21 // XXX rename this atrocity
#define UINT32_BASE10_LEN 12 // XXX number of base10 digits needed to store all values of 32 bit integer

/* Wait for input in ms */
#define POLL_TIMEOUT 10

#define FMSG 0x0
#define FCONNECT 0x1
#define FDISCONNECT 0x2
#define ECONNDROPPED 0x3

/* Errors */
#define ECONNCLSD -1 

#endif
