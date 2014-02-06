/*
 * BabelTrace - LTTng live Output
 *
 * Copyright 2013 Julien Desfossez <jdesfossez@efficios.com>
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

#include <babeltrace/ctf-text/types.h>
#include <babeltrace/format.h>
#include <babeltrace/babeltrace-internal.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <glib.h>
#include <unistd.h>
#include <stdlib.h>
#include "lttng-live-functions.h"

/*
 * hostname parameter needs to hold NAME_MAX chars.
 */
static int parse_url(const char *path, char *hostname, int *port,
		uint64_t *session_id, GArray *session_ids)
{
	char remain[3][NAME_MAX];
	int ret = -1, proto, proto_offset = 0;
	size_t path_len = strlen(path);
	char *str, *strctx;

	/*
	 * Since sscanf API does not allow easily checking string length
	 * against a size defined by a macro. Test it beforehand on the
	 * input. We know the output is always <= than the input length.
	 */
	if (path_len > NAME_MAX) {
		goto end;
	}
	ret = sscanf(path, "net%d://", &proto);
	if (ret < 1) {
		proto = 4;
		/* net:// */
		proto_offset = strlen("net://");
	} else {
		/* net4:// or net6:// */
		proto_offset = strlen("netX://");
	}
	if (proto_offset > path_len) {
		goto end;
	}
	/* TODO : parse for IPv6 as well */
	/* Parse the hostname or IP */
	ret = sscanf(&path[proto_offset], "%[a-zA-Z.0-9%-]%s",
		hostname, remain[0]);
	if (ret == 2) {
		/* Optional port number */
		switch (remain[0][0]) {
		case ':':
			ret = sscanf(remain[0], ":%d%s", port, remain[1]);
			/* Optional session ID with port number */
			if (ret == 2) {
				ret = sscanf(remain[1], "/%s", remain[2]);
				/* Accept 0 or 1 (optional) */
				if (ret < 0) {
					goto end;
				}
			}
			break;
		case '/':
			/* Optional session ID */
			ret = sscanf(remain[0], "/%s", remain[2]);
			/* Accept 0 or 1 (optional) */
			if (ret < 0) {
				goto end;
			}
			break;
		default:
			fprintf(stderr, "[error] wrong delimitor : %c\n",
				remain[0][0]);
			ret = -1;
			goto end;
		}
	}

	if (*port < 0)
		*port = LTTNG_DEFAULT_NETWORK_VIEWER_PORT;

	if (strlen(remain[2]) == 0) {
		printf_verbose("Connecting to hostname : %s, port : %d, "
				"proto : IPv%d\n",
				hostname, *port, proto);
		ret = 0;
		goto end;
	}

	printf_verbose("Connecting to hostname : %s, port : %d, "
			"session id(s) : %s, proto : IPv%d\n",
			hostname, *port, remain[2], proto);
	str = strtok_r(remain[2], ",", &strctx);
	do {
		char *endptr;
		uint64_t id;

		id = strtoull(str, &endptr, 0);
		if (*endptr != '\0' || str == endptr || errno != 0) {
			fprintf(stderr, "[error] parsing session id\n");
			ret = -1;
			goto end;
		}
		g_array_append_val(session_ids, id);
	} while ((str = strtok_r(NULL, ",", &strctx)));

	ret = 0;

end:
	return ret;
}

static int lttng_live_open_trace_read(const char *path)
{
	char hostname[NAME_MAX];
	int port = -1;
	uint64_t session_id = -1ULL;
	int ret = 0;
	struct lttng_live_ctx ctx;

	ctx.session = g_new0(struct lttng_live_session, 1);

	/* We need a pointer to the context from the packet_seek function. */
	ctx.session->ctx = &ctx;

	/* HT to store the CTF traces. */
	ctx.session->ctf_traces = g_hash_table_new(g_direct_hash,
			g_direct_equal);

	ctx.session_ids = g_array_new(FALSE, TRUE, sizeof(uint64_t));

	ret = parse_url(path, hostname, &port, &session_id, ctx.session_ids);
	if (ret < 0) {
		goto end_free;
	}

	ret = lttng_live_connect_viewer(&ctx, hostname, port);
	if (ret < 0) {
		fprintf(stderr, "[error] Connection failed\n");
		goto end_free;
	}
	printf_verbose("LTTng-live connected to relayd\n");

	ret = lttng_live_establish_connection(&ctx);
	if (ret < 0) {
		goto end_free;
	}

	if (ctx.session_ids->len == 0) {
		printf_verbose("Listing sessions\n");
		ret = lttng_live_list_sessions(&ctx, path);
		if (ret < 0) {
			fprintf(stderr, "[error] List error\n");
			goto end_free;
		}
	} else {
		lttng_live_read(&ctx);
	}

end_free:
	g_array_free(ctx.session_ids, TRUE);
	g_hash_table_destroy(ctx.session->ctf_traces);
	g_free(ctx.session);
	g_free(ctx.session->streams);
	return ret;
}

static
struct bt_trace_descriptor *lttng_live_open_trace(const char *path, int flags,
		void (*packet_seek)(struct bt_stream_pos *pos, size_t index,
			int whence), FILE *metadata_fp)
{
	struct ctf_text_stream_pos *pos;

	switch (flags & O_ACCMODE) {
	case O_RDONLY:
		/* OK */
		break;
	case O_RDWR:
		fprintf(stderr, "[error] lttng live plugin cannot be used as output plugin.\n");
		goto error;
	default:
		fprintf(stderr, "[error] Incorrect open flags.\n");
		goto error;
	}

	pos = g_new0(struct ctf_text_stream_pos, 1);
	pos->parent.rw_table = NULL;
	pos->parent.event_cb = NULL;
	pos->parent.trace = &pos->trace_descriptor;
	lttng_live_open_trace_read(path);
	return &pos->trace_descriptor;

error:
	return NULL;
}

static
int lttng_live_close_trace(struct bt_trace_descriptor *td)
{
	struct ctf_text_stream_pos *pos =
		container_of(td, struct ctf_text_stream_pos,
			trace_descriptor);
	free(pos);
	return 0;
}

static
struct bt_format lttng_live_format = {
	.open_trace = lttng_live_open_trace,
	.close_trace = lttng_live_close_trace,
};

static
void __attribute__((constructor)) lttng_live_init(void)
{
	int ret;

	lttng_live_format.name = g_quark_from_static_string("lttng-live");
	ret = bt_register_format(&lttng_live_format);
	assert(!ret);
}

static
void __attribute__((destructor)) lttng_live_exit(void)
{
	bt_unregister_format(&lttng_live_format);
}
