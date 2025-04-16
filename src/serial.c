#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>

#include "packet.h"
#include "serial.h"


#ifdef _DEBUG_
#define DEBUG(msg, ...)                                 \
	do {                                                \
		fprintf(stdout, "[debug] " msg, ##__VA_ARGS__); \
	} while(0)
#else
#define DEBUG(msg, ...) \
	{}
#endif


context_t* serial_init() {
	context_t* ctx = (context_t*)malloc(sizeof(context_t));
	ctx->loop = uv_default_loop();
	ctx->fd = -ENOENT;
	ctx->is_recv = 0;
	ctx->recv_msg_cb = NULL;
	return ctx;
}


void on_fs_close(uv_fs_t* req) {
	((context_t*)req->data)->fd = -ENOENT;
	uv_fs_req_cleanup(req);
}


void on_fs_open(uv_fs_t*);


void on_poll(uv_poll_t* handle, int stat, int events) {
	if(stat < 0) {
		fprintf(stderr, " [err] %s\n", uv_strerror(stat));
		return;
	}
	if(!(events & UV_READABLE)) {
		return;
	}

	context_t* ctx = handle->data;

	char rbuf[sizeof(packet_t) + BUF_SIZE];
	char* rbuf_bg = rbuf + sizeof(packet_t);
	size_t rsize = read(ctx->fd, rbuf_bg, BUF_SIZE);
	const char* rbuf_ed = rbuf_bg + rsize - 1;

	if(rsize == 0) {
		fprintf(stderr, " [err] no more data\n");
		uv_poll_stop(handle);
		uv_close((uv_handle_t*)handle, NULL);
		DEBUG("stop polling\n");
		ctx->close_req.data = ctx;
		uv_fs_close(ctx->loop, &ctx->close_req, ctx->fd,
		            on_fs_close);
		ctx->open_req.data = ctx;
		uv_fs_open(ctx->loop, &ctx->open_req,
		           ctx->tty_file_path_buf,
		           O_RDONLY | O_NONBLOCK, 0, on_fs_open);
		fprintf(stdout, "[info] close and reopen serial\n");
		return;
	}

	static char pbuf[sizeof(packet_t)] = {0};
	static int psize = 0;

	if(psize != 0) {
		rbuf_bg -= psize;
		memcpy(rbuf_bg, pbuf, psize);
		psize = 0;
	}

	for(char* o = rbuf_bg; o <= rbuf_ed; o++) {
		if(*o != PACKET_HEAD)
			continue;
		if(rbuf_ed - o + 1 < sizeof(packet_t)) {
			psize = rbuf_ed - o + 1;
			memcpy(pbuf, o, psize);
			break;
		}
		//if(*(o + sizeof(packet_t)) != PACKET_HEAD)
		//	continue;
		ctx->recv_msg_cb(
		    *(struct msg_t*)(o + sizeof(char)));
	}
}


void on_fs_open(uv_fs_t* req) {
	context_t* ctx = req->data;
	if(req->result >= 0) {
		// 文件存在并已被打开
		ctx->fd = req->result;

		struct termios opt;
		tcflush(ctx->fd, TCIOFLUSH);
		tcgetattr(ctx->fd, &opt);
		cfsetospeed(&opt, SERIAL_SPEED);
		cfsetispeed(&opt, SERIAL_SPEED);
		opt.c_cflag &= ~CSIZE;
		opt.c_cflag |= CS8;
		opt.c_cflag &= ~PARENB;
		opt.c_cflag &= ~INPCK;
		opt.c_cflag &= ~CSTOPB;
		tcsetattr(ctx->fd, TCSANOW, &opt);
		fprintf(stdout, "[info] serial opened\n");

		if(ctx->is_recv) {
			ctx->poll_handle.data = ctx;
			uv_poll_init(ctx->loop, &ctx->poll_handle,
			             ctx->fd);
			uv_poll_start(&ctx->poll_handle, UV_READABLE,
			              on_poll);
			DEBUG("start polling\n");
		}
	} else if(req->result == -ENOENT) {
		// 文件不存在
		// 说明文件一开始就不存在或被删除
		if(ctx->fd >= 0) {
			if(ctx->is_recv) {
				ctx->poll_handle.data = ctx;
				uv_poll_stop(&ctx->poll_handle);
				uv_close((uv_handle_t*)&ctx->poll_handle,
				         NULL);
				DEBUG("stop polling\n");
			}
			ctx->close_req.data = ctx;
			uv_fs_close(ctx->loop, &ctx->close_req, ctx->fd,
			            on_fs_close);
			DEBUG("close serial\n");
		}
		fprintf(stderr, "[warn] serial not found\n");
		fprintf(stdout, "[info] waiting for serial...\n");
	} else {
		fprintf(stderr, " [err] %s\n",
		        uv_strerror(req->result));
	}
	uv_fs_req_cleanup(req);
}


void on_event(uv_fs_event_t* handle, const char* fname,
              int events, int stat) {
	context_t* ctx = handle->data;

	if(stat < 0) {
		fprintf(stderr, " [err] %s\n", uv_strerror(stat));
		return;
	}
	if(!(events & UV_RENAME)) {
		return;
	}
	DEBUG("file status change: '%s'\n", fname);
	if(!fname || strcmp(fname, ctx->tty_file_buf) != 0) {
		return;
	}

	ctx->open_req.data = ctx;
	uv_fs_open(ctx->loop, &ctx->open_req,
	           ctx->tty_file_path_buf,
	           O_RDONLY | O_NONBLOCK, 0, on_fs_open);
}


void serial_open(context_t* ctx, const char* fpath) {
	// 定位到文件名
	for(const char* offset = fpath; *offset != '\0';
	    offset++) {
		if(*offset == '/')
			fpath = offset + 1;
	}

	// 添加前缀目录
	char* tty_file_prefix = "/dev";
	sprintf(ctx->tty_file_path_buf, "%s/%s",
	        tty_file_prefix, fpath);
	strcpy(ctx->tty_file_buf, fpath);
	fprintf(stdout, "[info] serial path: '%s'\n",
	        ctx->tty_file_path_buf);

	// 监控前缀目录
	// 当被监视文件状态发生变化, 尝试打开并读取
	// 如此实现自动重连
	ctx->event_handle.data = ctx;
	uv_fs_event_init(ctx->loop, &ctx->event_handle);
	uv_fs_event_start(&ctx->event_handle, on_event,
	                  tty_file_prefix, 0);
	DEBUG("begin watching path: '%s'\n", tty_file_prefix);

	// 在一开始就有目标文件时, 文件状态不会变化
	// 手动打开读取
	ctx->open_req.data = ctx;
	uv_fs_open(
	    ctx->loop, &ctx->open_req, ctx->tty_file_path_buf,
	    O_RDONLY | O_NOCTTY | O_NONBLOCK, 0, on_fs_open);
}


void serial_recv_init(context_t* ctx, msg_cb_t cb) {
	if(ctx->is_recv)
		return;
	ctx->is_recv = 1;
	ctx->recv_msg_cb = cb;
}


void serial_run(context_t* ctx) {
	uv_run(ctx->loop, UV_RUN_DEFAULT);
}


void on_poll_close(uv_handle_t* handle) {
	context_t* ctx = handle->data;
	ctx->is_recv = 0;
	ctx->recv_msg_cb = NULL;
}


void serial_recv_term(context_t* ctx) {
	if(!ctx->is_recv || ctx->fd < 0)
		return;
	ctx->poll_handle.data = ctx;
	uv_poll_stop(&ctx->poll_handle);
	uv_close((uv_handle_t*)&ctx->poll_handle,
	         on_poll_close);
	DEBUG("stop polling\n");
}


void serial_close(context_t* ctx) {
	ctx->event_handle.data = ctx;
	uv_fs_event_stop(&ctx->event_handle);
	uv_close((uv_handle_t*)&ctx->event_handle, NULL);
	DEBUG("stop watching\n");

	if(ctx->fd < 0)
		return;
	ctx->close_req.data = ctx;
	uv_fs_close(ctx->loop, &ctx->close_req, ctx->fd,
	            on_fs_close);
	DEBUG("close serial\n");
}


void serial_free(context_t* ctx) {
	uv_loop_close(ctx->loop);
	free(ctx);
	ctx = NULL;
	fprintf(stdout, "[info] exit\n");
}
