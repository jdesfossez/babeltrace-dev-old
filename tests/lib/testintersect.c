/*
 * test-ctf-writer.c
 *
 * CTF Writer test
 *
 * Copyright 2013 - 2015 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _GNU_SOURCE
#include <babeltrace/ctf-writer/writer.h>
#include <babeltrace/ctf-writer/clock.h>
#include <babeltrace/ctf-writer/stream.h>
#include <babeltrace/ctf-writer/event.h>
#include <babeltrace/ctf-writer/event-types.h>
#include <babeltrace/ctf-writer/event-fields.h>
#include <babeltrace/ctf-ir/stream-class.h>
#include <babeltrace/ctf-ir/stream-internal.h>
#include <babeltrace/ref.h>
#include <babeltrace/ctf/events.h>
#include <babeltrace/values.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/utsname.h>
#include <babeltrace/compat/limits.h>
#include <string.h>
#include <assert.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include "tap/tap.h"
#include <math.h>
#include <float.h>

static struct bt_ctf_field_type *uint_32_type, *uint_64_type;
static struct bt_ctf_field *integer_field, *stream_field, *begin_field, *end_field;
static struct bt_ctf_event *simple_event;
static struct bt_ctf_clock *btclock;

void create_packet(struct bt_ctf_stream *stream, int value, uint64_t begin,
		uint64_t end, uint64_t ts, uint64_t seq_num)
{
	struct bt_ctf_field *packet_context;
	struct bt_ctf_field *packet_context_field;

	integer_field = bt_ctf_field_create(uint_32_type);
	bt_ctf_field_unsigned_integer_set_value(integer_field, value);
	ok(bt_ctf_event_set_payload(simple_event, "dummy_value",
		integer_field) == 0, "Use bt_ctf_event_set_payload to set a manually allocated field");

	stream_field = bt_ctf_field_create(uint_32_type);
	bt_ctf_field_unsigned_integer_set_value(stream_field, stream->id);
	ok(bt_ctf_event_set_payload(simple_event, "tracefile_id",
		stream_field) == 0, "Use bt_ctf_event_set_payload to set a manually allocated field");

	begin_field = bt_ctf_field_create(uint_32_type);
	bt_ctf_field_unsigned_integer_set_value(begin_field, begin);
	bt_ctf_event_set_payload(simple_event, "packet_begin", begin_field);

	end_field = bt_ctf_field_create(uint_32_type);
	bt_ctf_field_unsigned_integer_set_value(end_field, end);
	bt_ctf_event_set_payload(simple_event, "packet_end", end_field);

	packet_context = bt_ctf_stream_get_packet_context(stream);
	ok(packet_context,
		"bt_ctf_stream_get_packet_context returns a packet context");
	packet_context_field = bt_ctf_field_structure_get_field(packet_context,
		"timestamp_begin");
	ok(packet_context_field,
		"Packet context contains the default packet_size field.");
	ok(bt_ctf_field_unsigned_integer_set_value(packet_context_field, begin) == 0,
		"Custom packet context field value successfully set.");
	bt_put(packet_context_field);
	packet_context_field = bt_ctf_field_structure_get_field(packet_context,
		"timestamp_end");
	ok(packet_context_field,
		"Packet context contains the default packet_size field.");
	ok(bt_ctf_field_unsigned_integer_set_value(packet_context_field, end) == 0,
		"Custom packet context field value successfully set.");
	bt_put(packet_context_field);


	packet_context = bt_ctf_stream_get_packet_context(stream);
	packet_context_field = bt_ctf_field_structure_get_field(packet_context,
		"packet_seq_num");
	bt_ctf_field_unsigned_integer_set_value(packet_context_field, seq_num);


	ok(bt_ctf_clock_set_time(btclock, ts) == 0, "clock");

	ok(bt_ctf_stream_append_event(stream, simple_event) == 0,
			"Append simple event to trace stream");
	ok(bt_ctf_stream_flush(stream) == 0,
		"Flush trace stream with one event");
}


int main(int argc, char **argv)
{
	char trace_path[] = "/tmp/ctfwriter_XXXXXX";
	char metadata_path[sizeof(trace_path) + 9];
	const char *clock_name = "test_clock";
	const char *clock_description = "This is a test clock";
	const uint64_t frequency = 1000000000ULL;
	const uint64_t offset_s = 1351530929945824323;
	const uint64_t precision = 10;
	const int is_absolute = 0xFF;
	struct bt_ctf_writer *writer;
	char hostname[BABELTRACE_HOST_NAME_MAX];
	struct bt_ctf_stream_class *stream_class;
	struct bt_ctf_stream *stream1, *stream2;
	struct bt_ctf_field_type *packet_header_type, *packet_context_type;

	struct bt_ctf_trace *trace;
	int ret;
	struct bt_ctf_event_class *simple_event_class =
		bt_ctf_event_class_create("dummy_event");

	if (!mkdtemp(trace_path)) {
		perror("# perror");
	}

	strcpy(metadata_path, trace_path);
	strcat(metadata_path + sizeof(trace_path) - 1, "/metadata");

	writer = bt_ctf_writer_create(trace_path);
	trace = bt_ctf_writer_get_trace(writer);
	bt_ctf_trace_set_byte_order(trace, BT_CTF_BYTE_ORDER_BIG_ENDIAN);

	/* Add environment context to the trace */
	ret = gethostname(hostname, sizeof(hostname));
	if (ret < 0) {
		return ret;
	}
	ok(bt_ctf_writer_add_environment_field(writer, "host", hostname) == 0, "hostname");
	btclock = bt_ctf_clock_create(clock_name);
	ok(btclock, "Clock created sucessfully");
	ok(bt_ctf_clock_set_description(btclock, clock_description) == 0,
		"Clock description set successfully");
	ok(bt_ctf_clock_set_frequency(btclock, frequency) == 0,
		"Set clock frequency");
	ok(bt_ctf_clock_set_offset_s(btclock, offset_s) == 0,
		"Set clock offset (seconds)");
	ok(bt_ctf_clock_set_precision(btclock, precision) == 0,
		"Set clock precision");
	ok(bt_ctf_clock_set_is_absolute(btclock, is_absolute) == 0,
		"Set clock absolute property");
	ok(bt_ctf_writer_add_clock(writer, btclock) == 0,
		"Add clock to writer instance");

	/* Define a stream class */
	stream_class = bt_ctf_stream_class_create("test_stream");
	ok(stream_class, "Create stream class");
	ok(bt_ctf_stream_class_set_clock(stream_class, btclock) == 0,
		"Set a stream class' clock");

	packet_header_type = bt_ctf_trace_get_packet_header_type(trace);
	ok(packet_header_type,
		"bt_ctf_trace_get_packet_header_type returns a packet header");

	packet_context_type = bt_ctf_stream_class_get_packet_context_type(stream_class);
	ok(packet_context_type,
		"bt_ctf_stream_class_get_packet_context_type returns a packet context type.");
	ok(bt_ctf_field_type_get_type_id(packet_context_type) == CTF_TYPE_STRUCT,
		"Packet context is a structure");
	uint_64_type = bt_ctf_field_type_integer_create(64);
	ret = bt_ctf_field_type_structure_add_field(packet_context_type,
		uint_64_type, "packet_seq_num");
	ok(ret == 0, "Packet context field added successfully");


	/*
	ret_field_type = bt_ctf_field_type_structure_get_field_type_by_name(
		packet_header_type, "magic");
	bt_put(ret_field_type);
		*/

	stream1 = bt_ctf_writer_create_stream(writer, stream_class);
	ok(stream1, "Instanciate a stream class from writer");

	stream2 = bt_ctf_writer_create_stream(writer, stream_class);
	ok(stream2, "Instanciate a stream class from writer");

	uint_32_type = bt_ctf_field_type_integer_create(32);
	uint_64_type = bt_ctf_field_type_integer_create(64);
	ok(bt_ctf_event_class_add_field(simple_event_class, uint_32_type,
		"dummy_value") == 0, "Add field");
	ok(bt_ctf_event_class_add_field(simple_event_class, uint_32_type,
		"tracefile_id") == 0, "Add field");
	ok(bt_ctf_event_class_add_field(simple_event_class, uint_32_type,
		"packet_begin") == 0, "Add field");
	ok(bt_ctf_event_class_add_field(simple_event_class, uint_32_type,
		"packet_end") == 0, "Add field");
	bt_ctf_stream_class_add_event_class(stream_class, simple_event_class);
	simple_event = bt_ctf_event_create(simple_event_class);
	ok(simple_event,
		"Instantiate an event containing a single integer field");

	integer_field = bt_ctf_field_create(uint_32_type);
	stream_field = bt_ctf_field_create(uint_32_type);
	begin_field = bt_ctf_field_create(uint_32_type);
	end_field = bt_ctf_field_create(uint_32_type);
	ok(bt_ctf_event_set_payload(simple_event, "dummy_value",
		integer_field) == 0, "Use bt_ctf_event_set_payload to set a manually allocated field");
	bt_ctf_event_set_payload(simple_event, "tracefile_id", stream_field);
	bt_ctf_event_set_payload(simple_event, "packet_begin", begin_field);
	bt_ctf_event_set_payload(simple_event, "packet_end", end_field);

#if 0
	/*
	 * 3eventsintersect
	 * Packets:
	 * - stream 1: [0, 20] [21, 40] [41, 60] [61, 80] [81, 100]
	 * - stream 2: [70, 90] [91, 110] [111, 120]
	 *
	 * Intersection: [70, 100], so we should see events from the second
	 * packet of stream1 and from the only packet of stream 2.
	*/
	create_packet(stream1, 0, 0, 20, 10);
	create_packet(stream1, 1, 21, 40, 21);
	create_packet(stream1, 2, 41, 60, 42);
	create_packet(stream2, 3, 70, 90, 71);
	create_packet(stream1, 4, 61, 80, 72);
	create_packet(stream1, 5, 81, 100, 82);
	create_packet(stream2, 6, 91, 110, 101);
	create_packet(stream2, 7, 111, 120, 112);

	/*
	 * nointersect
	 * Packets:
	 * - stream 1: [0, 20] [21, 40] [41, 60]
	 * - stream 2: [70, 90] [91, 110] [111, 120]
	 *
	 * Intersection: [70, 100], so we should see events from the second
	 * packet of stream1 and from the only packet of stream 2.
	*/
	create_packet(stream1, 0, 0, 20, 10);
	create_packet(stream1, 1, 21, 40, 21);
	create_packet(stream1, 2, 41, 60, 42);
	create_packet(stream2, 3, 70, 90, 71);
	create_packet(stream2, 4, 91, 110, 101);
	create_packet(stream2, 5, 111, 120, 112);

	/*
	 * Test the seq_num feature
	 */
	// no lost
	create_packet(stream1, 0, 0, 20, 10, 0);
	create_packet(stream1, 1, 21, 40, 21, 1);
	create_packet(stream1, 2, 41, 60, 42, 2);

	// no_lost_not_starting_at_0
	create_packet(stream1, 0, 0, 20, 10, 3);
	create_packet(stream1, 1, 21, 40, 21, 4);
	create_packet(stream1, 2, 41, 60, 42, 5);

	//2 lost before last
	create_packet(stream1, 0, 0, 20, 10, 0);
	create_packet(stream1, 1, 21, 40, 21, 1);
	create_packet(stream1, 2, 41, 60, 42, 4);
	// 2_streams_lost_in_1
	create_packet(stream1, 0, 0, 20, 10, 0);
	create_packet(stream1, 1, 21, 40, 21, 1);
	create_packet(stream1, 2, 41, 60, 42, 2);
	create_packet(stream2, 3, 70, 90, 71, 0);
	create_packet(stream1, 4, 61, 80, 72, 3);
	create_packet(stream1, 5, 81, 100, 82, 6);
	create_packet(stream2, 6, 91, 110, 101, 1);
	create_packet(stream2, 7, 111, 120, 112, 2);
#endif

	// 2_streams_lost_in_2
	create_packet(stream1, 0, 0, 20, 10, 0);
	create_packet(stream1, 1, 21, 40, 21, 1);
	create_packet(stream1, 2, 41, 60, 42, 2);
	create_packet(stream2, 3, 70, 90, 71, 0);
	create_packet(stream1, 4, 61, 80, 72, 3);
	create_packet(stream1, 5, 81, 100, 82, 6);
	create_packet(stream2, 6, 91, 110, 101, 4);
	create_packet(stream2, 7, 111, 120, 112, 6);

	bt_ctf_writer_flush_metadata(writer);

	fprintf(stdout, "babeltrace %s\n", trace_path);

	return 0;
}
