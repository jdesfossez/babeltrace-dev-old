#include <hiredis/hiredis.h>
#include <glib.h>
#include <stdio.h>
#include <babeltrace/context.h>
#include <babeltrace/trace-handle.h>
#include <babeltrace/ctf/callbacks.h>
#include "lttng-state.h"
#include "lttng-state-track.h"
#include "lua_scripts.h"

static
int add_session(struct lttng_state_ctx *ctx)
{
	redisReply *reply;
	redisContext *c = ctx->redis;
	int ret;

	reply = redisCommand(c, "SADD %s %s", "hostnames",
			ctx->traced_hostname);
	if (!reply) {
		ret = -1;
		goto end_free;
	}
	freeReplyObject(reply);

	reply = redisCommand(c, "SADD %s:sessions %s",
			ctx->traced_hostname, ctx->session_name);
	if (!reply) {
		ret = -1;
		goto end_free;
	}
	ret = 0;

end_free:
	freeReplyObject(reply);
	return ret;
}

static
int connect_redis(struct lttng_state_ctx *ctx)
{
	const char *hostname = "localhost";
	int port = 6379, ret;
	struct timeval timeout = { 1, 500000 }; // 1.5 seconds

	ctx->redis = redisConnectWithTimeout(hostname, port, timeout);
	if (ctx->redis == NULL || ctx->redis->err) {
		if (ctx->redis) {
			fprintf(stderr, "Connection error: %s\n", ctx->redis->errstr);
			redisFree(ctx->redis);
		} else {
			fprintf(stderr, "Connection error: can't allocate redis context\n");
		}
		ret = -1;
		goto end;
	}
	ret = 0;

end:
	return ret;
}

int lttng_state_init(struct lttng_state_ctx *ctx, struct bt_ctf_iter *iter)
{
	int ret;

	bt_ctf_iter_add_callback(iter,
			g_quark_from_static_string("sched_process_fork"),
			ctx, 0, handle_sched_process_fork, NULL, NULL, NULL);
	bt_ctf_iter_add_callback(iter,
			g_quark_from_static_string("sched_process_free"),
			ctx, 0, handle_sched_process_free, NULL, NULL, NULL);
	bt_ctf_iter_add_callback(iter,
			g_quark_from_static_string("sched_switch"),
			ctx, 0, handle_sched_switch, NULL, NULL, NULL);
	bt_ctf_iter_add_callback(iter,
			g_quark_from_static_string("sys_open"),
			ctx, 0, handle_sys_open, NULL, NULL, NULL);
	bt_ctf_iter_add_callback(iter,
			g_quark_from_static_string("sys_close"),
			ctx, 0, handle_sys_close, NULL, NULL, NULL);
	bt_ctf_iter_add_callback(iter,
			g_quark_from_static_string("exit_syscall"),
			ctx, 0, handle_exit_syscall, NULL, NULL, NULL);

	if (!ctx->redis) {
		ret = connect_redis(ctx);
		if (ret < 0)
			goto end;
	}
	ret = add_session(ctx);

end:
	return ret;
}

int lttng_state_process_event(struct lttng_state_ctx *ctx,
	struct bt_stream_pos *ppos, struct ctf_stream_definition *stream)
{
	return 0;
}
