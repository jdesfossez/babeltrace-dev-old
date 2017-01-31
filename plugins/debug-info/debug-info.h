#ifndef BABELTRACE_PLUGIN_DEBUG_INFO_H
#define BABELTRACE_PLUGIN_DEBUG_INFO_H

/*
 * Babeltrace - Debug information Plug-in
 *
 * Copyright (c) 2015 EfficiOS Inc.
 * Copyright (c) 2015 Antoine Busque <abusque@efficios.com>
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

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <babeltrace/babeltrace-internal.h>
#include <babeltrace/ctf-ir/event.h>
#include <babeltrace/ctf-ir/trace.h>
#include <babeltrace/ctf-ir/fields.h>
#include <babeltrace/ctf-ir/event-class.h>

struct debug_info_component {
	FILE *err;
};

struct debug_info_iterator {
	/* Map between struct bt_ctf_trace and struct bt_ctf_writer. */
	GHashTable *trace_map;
	/* Map between reader and writer stream. */
	GHashTable *stream_map;
	/* Map between reader and writer stream class. */
	GHashTable *stream_class_map;
	/* Input iterators associated with this output iterator. */
	GPtrArray *input_iterator_group;
	struct bt_notification *current_notification;
	struct bt_notification_iterator *input_iterator;
};

struct debug_info_source {
	/* Strings are owned by debug_info_source. */
	char *func;
	uint64_t line_no;
	char *src_path;
	/* short_src_path points inside src_path, no need to free. */
	const char *short_src_path;
	char *bin_path;
	/* short_bin_path points inside bin_path, no need to free. */
	const char *short_bin_path;
	/*
	 * Location within the binary. Either absolute (@0x1234) or
	 * relative (+0x4321).
	 */
	char *bin_loc;
};

BT_HIDDEN
enum bt_component_status debug_info_output_event(
		struct debug_info_component *writer, struct bt_ctf_event *event);
BT_HIDDEN
enum bt_component_status debug_info_new_packet(
		struct debug_info_component *writer, struct bt_ctf_packet *packet);
BT_HIDDEN
enum bt_component_status debug_info_close_packet(
		struct debug_info_component *writer, struct bt_ctf_packet *packet);

BT_HIDDEN
struct debug_info *debug_info_create(void);

BT_HIDDEN
void debug_info_destroy(struct debug_info *debug_info);

BT_HIDDEN
void debug_info_handle_event(struct bt_ctf_trace *trace_class,
		struct bt_ctf_event *event);

#if 0
static inline
void ctf_text_integer_write_debug_info(struct bt_stream_pos *ppos,
		struct bt_definition *definition)
{
	struct definition_integer *integer_definition =
			container_of(definition, struct definition_integer, p);
	struct ctf_text_stream_pos *pos = ctf_text_pos(ppos);
	struct debug_info_source *debug_info_src =
			integer_definition->debug_info_src;

	/* Print debug info if available */
	if (debug_info_src) {
		if (debug_info_src->func || debug_info_src->src_path ||
				debug_info_src->bin_path) {
			bool add_comma = false;

			fprintf(pos->fp, ", debug_info = { ");

			if (debug_info_src->bin_path) {
				fprintf(pos->fp, "bin = \"%s%s\"",
						opt_debug_info_full_path ?
						debug_info_src->bin_path :
						debug_info_src->short_bin_path,
						debug_info_src->bin_loc);
				add_comma = true;
			}

			if (debug_info_src->func) {
				if (add_comma) {
					fprintf(pos->fp, ", ");
				}

				fprintf(pos->fp, "func = \"%s\"",
						debug_info_src->func);
			}

			if (debug_info_src->src_path) {
				if (add_comma) {
					fprintf(pos->fp, ", ");
				}

				fprintf(pos->fp, "src = \"%s:%" PRIu64
						"\"",
						opt_debug_info_full_path ?
						debug_info_src->src_path :
						debug_info_src->short_src_path,
						debug_info_src->line_no);
			}

			fprintf(pos->fp, " }");
		}
	}
}

static inline
int trace_debug_info_create(struct bt_ctf_trace *trace)
{
	int ret = 0;
	struct bt_value *value;
	const char *str_value;
	enum bt_value_status rc;

	value = bt_ctf_trace_get_environment_field_value_by_name(trace,
			"domain");
	if (!value) {
		goto end;
	}
	rc = bt_value_string_get(value, &str_value);
	if (rc != BT_VALUE_STATUS_OK) {
		goto end_put_value;
	}
	if (strcmp(str_value, "ust") != 0) {
		goto end_put_value;
	}
	bt_put(value);

	value = bt_ctf_trace_get_environment_field_value_by_name(trace,
			"tracer_name");
	if (!value) {
		goto end;
	}
	rc = bt_value_string_get(value, &str_value);
	if (rc != BT_VALUE_STATUS_OK) {
		goto end_put_value;
	}
	if (strcmp(str_value, "lttng-ust") != 0) {
		goto end_put_value;
	}
	bt_put(value);

	trace->debug_info = debug_info_create();
	if (!trace->debug_info) {
		ret = -1;
		goto end;
	}
	ret = 0;
	goto end;

end_put_value:
	bt_put(value);
end:
	return ret;
}

static inline
void trace_debug_info_destroy(struct bt_ctf_trace *trace)
{
	debug_info_destroy(trace->debug_info);
}

static inline
void handle_debug_info_event(struct bt_ctf_event *event)
{
	debug_info_handle_event(event);
}
#endif

#endif /* BABELTRACE_PLUGIN_DEBUG_INFO_H */
