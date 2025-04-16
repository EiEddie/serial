#include <stdio.h>
#include <uv.h>

#include "serial.h"


static uv_signal_t signal_handle;


void msg_cb(struct msg_t msg) {
	printf("\t{msg} val:%d [%x]\n", msg.val, msg.val);
}


void on_signal(uv_signal_t* handle, int _) {
	context_t* ctx = handle->data;

	serial_recv_term(ctx);
	serial_close(ctx);

	uv_close((uv_handle_t*)handle, NULL);
}


int main(int argc, char* argv[]) {
	const char* fname = argv[1];
	context_t* ctx = serial_init();
	serial_open(ctx, fname);
	serial_recv_init(ctx, msg_cb);

	uv_signal_init(ctx->loop, &signal_handle);
	signal_handle.data = ctx;
	uv_signal_start(&signal_handle, on_signal, SIGINT);

	serial_run(ctx);

	serial_free(ctx);
	return 0;
}
