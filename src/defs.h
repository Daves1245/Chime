//#define DEBUG 1
// debug flags

#define DEBUG_RECVMSG 1

// Arbitrary
#define HEADER_LEN 4
#define MAX_RECV_LEN 100
#define MAX_SEND_LEN 100

#define MAX_PACKET_PAYLOAD_SIZE 1400
#define MAX_PACKETS_PER_MSG 256 // handle input up to 358400 bytes long

#define MAX_NAME_LEN 30
#define MAX_TEXT_LEN 3000

#define HASH_LEN 256 // length of hash

#define UINT64_BASE10_LEN 21 // XXX rename this atrocity
#define UINT32_BASE10_LEN 12 // XXX number of base10 digits needed to store all values of 32 bit integer

#define FMSG 0x0
#define FCONNECT 0x1
#define FDISCONNECT 0x2

