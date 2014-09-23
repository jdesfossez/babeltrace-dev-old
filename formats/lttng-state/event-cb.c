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
uint64_t get_cpu_id(const struct bt_ctf_event *event)
{
	const struct bt_definition *scope;
	uint64_t cpu_id;

	scope = bt_ctf_get_top_level_scope(event, BT_STREAM_PACKET_CONTEXT);
	cpu_id = bt_ctf_get_uint64(bt_ctf_get_field(event, scope, "cpu_id"));
	if (bt_ctf_field_get_error()) {
		fprintf(stderr, "[error] get cpu_id\n");
		return -1ULL;
	}

	return cpu_id;
}


enum bt_cb_ret handle_sched_process_fork(struct bt_ctf_event *call_data,
		void *private_data)
{
	const struct bt_definition *scope;
	int64_t child_tid, parent_pid, child_pid;
	uint64_t timestamp, cpu_id;
	char *child_comm;
	struct lttng_state_ctx *ctx = private_data;
	redisContext *c = ctx->redis;

	timestamp = bt_ctf_get_timestamp(call_data);
	if (timestamp == -1ULL)
		goto error;

	scope = bt_ctf_get_top_level_scope(call_data,
			BT_EVENT_FIELDS);
	child_comm = bt_ctf_get_char_array(bt_ctf_get_field(call_data,
				scope, "_child_comm"));
	if (bt_ctf_field_get_error()) {
		fprintf(stderr, "Missing child_comm context info\n");
		goto error;
	}

	child_tid = bt_ctf_get_int64(bt_ctf_get_field(call_data,
				scope, "_child_tid"));
	if (bt_ctf_field_get_error()) {
		fprintf(stderr, "Missing child_tid field\n");
		goto error;
	}

	child_pid = bt_ctf_get_int64(bt_ctf_get_field(call_data,
				scope, "_child_pid"));
	if (bt_ctf_field_get_error()) {
		fprintf(stderr, "Missing child_pid field\n");
		goto error;
	}

	parent_pid = bt_ctf_get_int64(bt_ctf_get_field(call_data,
				scope, "_parent_pid"));
	if (bt_ctf_field_get_error()) {
		fprintf(stderr, "Missing parent_pid field\n");
		goto error;
	}
	cpu_id = get_cpu_id(call_data);

	fprintf(stderr, "TS: %lu, %ld %s %ld %ld\n", timestamp, parent_pid, child_comm,
			child_tid, child_pid);

	redisAppendCommand(c, "EVALSHA %s 1 %s:%s %" PRId64 " %" PRId64 \
			" %s %" PRId64 " %" PRId64 " %" PRId64 " %" PRIu64,
			REDIS_SCHED_PROCESS_FORK,
			ctx->traced_hostname, ctx->session_name, timestamp,
			parent_pid, child_comm, child_tid, child_pid, cpu_id);
	nb_events++;

	return BT_CB_OK;

error:
	return BT_CB_ERROR_STOP;
}

enum bt_cb_ret handle_sched_process_free(struct bt_ctf_event *call_data,
		void *private_data)
{
	const struct bt_definition *scope;
	uint64_t timestamp;
	char *comm;
	int64_t tid;
	uint64_t cpu_id;
	struct lttng_state_ctx *ctx = private_data;
	redisContext *c = ctx->redis;

	timestamp = bt_ctf_get_timestamp(call_data);
	if (timestamp == -1ULL)
		goto error;

	scope = bt_ctf_get_top_level_scope(call_data,
			BT_EVENT_FIELDS);
	comm = bt_ctf_get_char_array(bt_ctf_get_field(call_data,
				scope, "_comm"));
	if (bt_ctf_field_get_error()) {
		fprintf(stderr, "Missing procname context info\n");
		goto error;
	}

	tid = bt_ctf_get_int64(bt_ctf_get_field(call_data,
				scope, "_tid"));
	if (bt_ctf_field_get_error()) {
		fprintf(stderr, "Missing tid field\n");
		goto error;
	}
	cpu_id = get_cpu_id(call_data);

	fprintf(stderr, "TERMINATED %ld\n", tid);
	redisAppendCommand(c, "EVALSHA %s 1 %s:%s %" PRId64 " %" PRIu64 \
			" %s %" PRId64,
			REDIS_SCHED_PROCESS_FREE,
			ctx->traced_hostname, ctx->session_name, timestamp,
			cpu_id, comm, tid);
	nb_events++;

	return BT_CB_OK;

error:
	return BT_CB_ERROR_STOP;

}

enum bt_cb_ret handle_sched_switch(struct bt_ctf_event *call_data,
		void *private_data)
{
	const struct bt_definition *scope;
	unsigned long timestamp;
	uint64_t cpu_id;
	char *prev_comm, *next_comm;
	int64_t prev_tid, next_tid;
	struct lttng_state_ctx *ctx = private_data;
	redisContext *c = ctx->redis;

	timestamp = bt_ctf_get_timestamp(call_data);
	if (timestamp == -1ULL)
		goto error;

	scope = bt_ctf_get_top_level_scope(call_data,
			BT_EVENT_FIELDS);
	prev_comm = bt_ctf_get_char_array(bt_ctf_get_field(call_data,
				scope, "_prev_comm"));
	if (bt_ctf_field_get_error()) {
		fprintf(stderr, "Missing prev_comm context info\n");
		goto error;
	}

	next_comm = bt_ctf_get_char_array(bt_ctf_get_field(call_data,
				scope, "_next_comm"));
	if (bt_ctf_field_get_error()) {
		fprintf(stderr, "Missing next_comm context info\n");
		goto error;
	}

	prev_tid = bt_ctf_get_int64(bt_ctf_get_field(call_data,
				scope, "_prev_tid"));
	if (bt_ctf_field_get_error()) {
		fprintf(stderr, "Missing prev_tid context info\n");
		goto error;
	}

	next_tid = bt_ctf_get_int64(bt_ctf_get_field(call_data,
				scope, "_next_tid"));
	if (bt_ctf_field_get_error()) {
		fprintf(stderr, "Missing next_tid context info\n");
		goto error;
	}

	cpu_id = get_cpu_id(call_data);

	redisAppendCommand(c, "EVALSHA %s 1 %s:%s %" PRId64 \
			" %s %" PRId64 " %s %" PRId64 " %" PRIu64,
			REDIS_SCHED_SWITCH,
			ctx->traced_hostname, ctx->session_name, timestamp,
			prev_comm, prev_tid, next_comm, next_tid, cpu_id);
	nb_events++;

	return BT_CB_OK;

error:
	return BT_CB_ERROR_STOP;
}

enum bt_cb_ret handle_sys_open(struct bt_ctf_event *call_data,
		void *private_data)
{

	const struct bt_definition *scope;
	unsigned long timestamp;
	uint64_t cpu_id;
	char *filename;
	struct lttng_state_ctx *ctx = private_data;
	redisContext *c = ctx->redis;

	timestamp = bt_ctf_get_timestamp(call_data);
	if (timestamp == -1ULL)
		goto error;

	cpu_id = get_cpu_id(call_data);

	scope = bt_ctf_get_top_level_scope(call_data,
			BT_EVENT_FIELDS);
	filename = bt_ctf_get_string(bt_ctf_get_field(call_data,
				scope, "_filename"));
	if (bt_ctf_field_get_error()) {
		fprintf(stderr, "Missing filename info\n");
		goto error;
	}

	redisAppendCommand(c, "EVALSHA %s 1 %s:%s %" PRId64 \
			" %" PRId64 " %s",
			REDIS_SYS_OPEN, ctx->traced_hostname,
			ctx->session_name, timestamp, cpu_id, filename);
	nb_events++;

	return BT_CB_OK;

error:
	return BT_CB_ERROR_STOP;
}

enum bt_cb_ret handle_exit_syscall(struct bt_ctf_event *call_data,
		void *private_data)
{
	const struct bt_definition *scope;
	unsigned long timestamp;
	int64_t ret;
	uint64_t cpu_id;
	struct lttng_state_ctx *ctx = private_data;
	redisContext *c = ctx->redis;

	timestamp = bt_ctf_get_timestamp(call_data);
	if (timestamp == -1ULL)
		goto error;

	scope = bt_ctf_get_top_level_scope(call_data,
			BT_EVENT_FIELDS);
	ret = bt_ctf_get_int64(bt_ctf_get_field(call_data,
				scope, "_ret"));
	if (bt_ctf_field_get_error()) {
		fprintf(stderr, "Missing ret info\n");
		goto error;
	}

	cpu_id = get_cpu_id(call_data);

	redisAppendCommand(c, "EVALSHA %s 1 %s:%s %" PRId64 \
			" %" PRId64 " %" PRId64,
			REDIS_EXIT_SYSCALL, ctx->traced_hostname,
			ctx->session_name, timestamp, cpu_id, ret);
	nb_events++;

	return BT_CB_OK;

error:
	return BT_CB_ERROR_STOP;
}

enum bt_cb_ret handle_sys_close(struct bt_ctf_event *call_data,
		void *private_data)
{
	const struct bt_definition *scope;
	unsigned long timestamp;
	uint64_t cpu_id, fd;
	struct lttng_state_ctx *ctx = private_data;
	redisContext *c = ctx->redis;

	timestamp = bt_ctf_get_timestamp(call_data);
	if (timestamp == -1ULL)
		goto error;

	cpu_id = get_cpu_id(call_data);

	scope = bt_ctf_get_top_level_scope(call_data,
			BT_EVENT_FIELDS);
	fd = bt_ctf_get_uint64(bt_ctf_get_field(call_data,
				scope, "_fd"));
	if (bt_ctf_field_get_error()) {
		fprintf(stderr, "Missing fd info\n");
		goto error;
	}

	redisAppendCommand(c, "EVALSHA %s 1 %s:%s %" PRId64 \
			" %" PRId64 " %" PRIu64,
			REDIS_SYS_CLOSE, ctx->traced_hostname,
			ctx->session_name, timestamp, cpu_id, fd);
	nb_events++;

	return BT_CB_OK;

error:
	return BT_CB_ERROR_STOP;
}

