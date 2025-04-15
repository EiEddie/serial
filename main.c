#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <uv.h>

#include "packet.h"


#ifdef _DEBUG_
#define DEBUG(msg, ...)                                 \
	do {                                                \
		fprintf(stdout, "[debug] " msg, ##__VA_ARGS__); \
	} while(0)
#else
#define DEBUG(msg, ...) \
	{}
#endif

#define BUF_SIZE 1024

static char tty_file_path_buf[64];
static char tty_file_buf[64];
static uv_fs_t open_req;
static int fd = -ENOENT;
static uv_poll_t poll_handle;


typedef struct {
	char head;
	struct msg_t msg;
} __attribute__((packed)) packet_t;


/// @brief on_open
void check_tty_file(uv_fs_t* req);


void on_poll(uv_poll_t* handle, int stat, int events) {
	if(stat < 0) {
		fprintf(stderr, " [err] %s\n", uv_strerror(stat));
		return;
	}
	if(!(events & UV_READABLE)) {
		return;
	}

	char rbuf[sizeof(packet_t) + BUF_SIZE];
	char* rbuf_bg = rbuf + sizeof(packet_t);
	size_t rsize = read(fd, rbuf_bg, BUF_SIZE);
	const char* rbuf_ed = rbuf_bg + rsize - 1;

	if(rsize == 0) {
		fprintf(stderr, " [err] no more data\n");
		uv_poll_stop(handle);
		uv_close((uv_handle_t*)handle, NULL);
		DEBUG("stop polling\n");
		uv_fs_t close_req;
		uv_fs_close(uv_default_loop(), &close_req, fd,
		            NULL);
		uv_fs_req_cleanup(&close_req);
		fd = -ENOENT;
		uv_fs_open(uv_default_loop(), &open_req,
		           tty_file_path_buf, O_RDONLY | O_NONBLOCK,
		           0, check_tty_file);
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
		packet_msg_cb(*(struct msg_t*)(o + sizeof(char)));
	}
}


/// @brief on_open
void check_tty_file(uv_fs_t* req) {
	if(req->result >= 0) {
		// 文件存在并已被打开
		fd = req->result;
		fprintf(stdout, "[info] serial opened\n");
		uv_poll_init(uv_default_loop(), &poll_handle, fd);
		uv_poll_start(&poll_handle, UV_READABLE, on_poll);
		DEBUG("start polling\n");
	} else if(req->result == -ENOENT) {
		// 文件不存在
		// 说明文件一开始就不存在或被删除
		if(fd >= 0) {
			uv_poll_stop(&poll_handle);
			uv_close((uv_handle_t*)&poll_handle, NULL);
			uv_fs_t close_req;
			uv_fs_close(uv_default_loop(), &close_req, fd,
			            NULL);
			fd = -ENOENT;
			uv_fs_req_cleanup(&close_req);
			DEBUG("stop polling and close file\n");
		}
		fprintf(stderr, "[warn] serial not found\n");
		fprintf(stdout, "[info] waiting for serial...\n");
	} else {
		fprintf(stderr, " [err] %s\n",
		        uv_strerror(req->result));
	}
	uv_fs_req_cleanup(req);
}


/// @brief on_event
void watch_dir(uv_fs_event_t* handle, const char* fname,
               int events, int stat) {
	if(stat < 0) {
		fprintf(stderr, " [err] %s\n", uv_strerror(stat));
		return;
	}
	if(!(events & UV_RENAME)) {
		return;
	}
	DEBUG("file status change: '%s'\n", fname);
	if(!fname || strcmp(fname, tty_file_buf) != 0) {
		return;
	}

	uv_fs_open(uv_default_loop(), &open_req,
	           tty_file_path_buf, O_RDONLY | O_NONBLOCK, 0,
	           check_tty_file);
}


int main(int argc, char* argv[]) {
	DEBUG("packet size: %ld\n", sizeof(packet_t));
	DEBUG("message size: %ld\n", sizeof(struct msg_t));
	const char* tty_file = argv[1];

	// 定位到文件名
	for(const char* offset = tty_file; *offset != '\0';
	    offset++) {
		if(*offset == '/')
			tty_file = offset + 1;
	}

	// 添加前缀目录
	char* tty_file_prefix = "/tmp";
	sprintf(tty_file_path_buf, "%s/%s", tty_file_prefix,
	        tty_file);
	strcpy(tty_file_buf, tty_file);
	tty_file = tty_file_path_buf;
	fprintf(stdout, "[info] serial path: '%s'\n", tty_file);

	// 监控前缀目录
	// 当被监视文件状态发生变化, 尝试打开并读取
	// 如此实现自动重连
	uv_fs_event_t dir_watcher;
	uv_fs_event_init(uv_default_loop(), &dir_watcher);
	uv_fs_event_start(&dir_watcher, watch_dir,
	                  tty_file_prefix, 0);
	DEBUG("begin watching path: '%s'\n", tty_file_prefix);

	// 在一开始就有目标文件时, 文件状态不会变化
	// 手动打开读取
	uv_fs_open(uv_default_loop(), &open_req,
	           tty_file_path_buf, O_RDONLY | O_NONBLOCK, 0,
	           check_tty_file);

	uv_run(uv_default_loop(), UV_RUN_DEFAULT);

	if(fd >= 0) {
		uv_fs_t close_req;
		uv_fs_close(uv_default_loop(), &close_req, fd,
		            NULL);
		fd = -ENOENT;
		uv_fs_req_cleanup(&close_req);
		DEBUG("close and clean up serial\n");
	}

	uv_loop_close(uv_default_loop());
	uv_fs_event_stop(&dir_watcher);
	uv_close((uv_handle_t*)&dir_watcher, NULL);
	DEBUG("stop watching\n");
	uv_fs_req_cleanup(&open_req);
	return 0;
}
