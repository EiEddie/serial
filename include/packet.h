#ifndef __H_PACKET_
#define __H_PACKET_

#include <stdint.h>


#define PACKET_HEAD (char)0xa5

struct msg_t {
	// TODO
	uint32_t val;
} __attribute__((packed));


typedef void (*msg_cb_t)(struct msg_t);


#endif
