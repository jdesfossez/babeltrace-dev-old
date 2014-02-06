#ifndef _LTTNG_LIVE_FUNCTIONS_H
#define _LTTNG_LIVE_FUNCTIONS_H

/*
 * Copyright 2013 Julien Desfossez <julien.desfossez@efficios.com>
 *                Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdint.h>

#define LTTNG_METADATA_PATH_TEMPLATE		"/tmp/lttng-live-XXXXXX"
#define LTTNG_DEFAULT_NETWORK_VIEWER_PORT	5344

#define LTTNG_LIVE_MAJOR			2
#define LTTNG_LIVE_MINOR			4

struct lttng_live_ctx {
	char traced_hostname[NAME_MAX];
	char session_name[NAME_MAX];
	char relay_hostname[NAME_MAX];
	int control_sock;
	int port;
	struct lttng_live_session *session;
	struct bt_context *bt_ctx;
	GArray *session_ids;
};

struct lttng_live_viewer_stream {
	uint64_t id;
	uint64_t mmap_size;
	int fd;
	int metadata_flag;
	int first_read;
	struct lttng_live_session *session;
	struct lttng_live_ctf_trace *ctf_trace;
	char path[PATH_MAX];
};

struct lttng_live_session {
	uint64_t live_timer_interval;
	uint64_t stream_count;
	struct lttng_live_ctx *ctx;
	struct lttng_live_viewer_stream *streams;
	GHashTable *ctf_traces;
};

struct lttng_live_ctf_trace {
	uint64_t ctf_trace_id;
	struct lttng_live_viewer_stream *metadata_stream;
	GPtrArray *streams;
	FILE *metadata_fp;
	int trace_id;
	int in_use;
};

/* Just used in listing. */
struct lttng_live_relay_session {
	uint32_t streams;
	uint32_t clients;
	uint32_t timer;
	char *name;
	char *hostname;
};

int lttng_live_connect_viewer(struct lttng_live_ctx *ctx);
int lttng_live_establish_connection(struct lttng_live_ctx *ctx);
int lttng_live_list_sessions(struct lttng_live_ctx *ctx, const char *path);
int lttng_live_attach_session(struct lttng_live_ctx *ctx, uint64_t id);
void lttng_live_read(struct lttng_live_ctx *ctx);
int lttng_live_get_new_streams(struct lttng_live_ctx *ctx, uint64_t id);

#endif /* _LTTNG_LIVE_FUNCTIONS_H */
