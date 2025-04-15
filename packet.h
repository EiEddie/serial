#ifndef __H_PACKET_
#define __H_PACKET_

#include <stdint.h>
#include <stdio.h>

#define PACKET_HEAD (char)0xa5

struct msg_t {
	// TODO
	uint32_t a;
} __attribute__((packed));


void packet_msg_cb(struct msg_t msg);

inline void packet_msg_cb(struct msg_t msg) {
	// TODO
	printf("\t{msg} a:%d [%x]\n", msg.a, msg.a);
}


#endif
