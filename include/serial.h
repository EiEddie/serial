#ifndef __H_SERIAL_
#define __H_SERIAL_

#include <uv.h>

#include "packet.h"


#define BUF_SIZE     1024
#define SERIAL_SPEED B115200


typedef struct {
	uv_loop_t* loop;
	char tty_file_buf[64];
	char tty_file_path_buf[64];

	uv_fs_t open_req;
	uv_fs_t close_req;
	int fd;

	int is_recv;
	msg_cb_t recv_msg_cb;

	uv_fs_event_t event_handle;
	uv_poll_t poll_handle;
} context_t;


typedef struct {
	char head;
	struct msg_t msg;
} __attribute__((packed)) packet_t;


context_t* serial_init();


void serial_open(context_t* ctx, const char* fpath);


void serial_recv_init(context_t* ctx, msg_cb_t cb);


void serial_run(context_t* ctx);


void serial_recv_term(context_t* ctx);


void serial_close(context_t* ctx);


void serial_free(context_t* ctx);


#endif
