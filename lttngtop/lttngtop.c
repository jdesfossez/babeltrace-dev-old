/*
 * Copyright (C) 2011-2012 Julien Desfossez
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation;
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
#include <config.h>
#include <stdio.h>
#include <stdint.h>
#include <babeltrace/babeltrace.h>
#include <babeltrace/ctf/events.h>
#include <babeltrace/ctf/callbacks.h>
#include <babeltrace/ctf/iterator.h>
#include <fcntl.h>
#include <pthread.h>
#include <popt.h>
#include <stdlib.h>
#include <ftw.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <fts.h>
#include <assert.h>
#include <sys/mman.h>
#include <lttng/lttng.h>
#if LTTNGTOP_MMAP_LIVE
#include <lttng/lttngtop-helper.h>
#include <babeltrace/lttngtopmmappacketseek.h>
#endif

#include "lttngtoptypes.h"
#include "cputop.h"
#include "iostreamtop.h"
#include "cursesdisplay.h"
#include "common.h"


#include "babeltrace/format.h"

#define DEFAULT_FILE_ARRAY_SIZE 1

#define LTTNGTOP_MMAP_LIVE 0

const char *opt_input_path;
static int opt_textdump;
static int opt_child;

int quit = 0;

struct lttngtop *copy;
pthread_t display_thread;
pthread_t timer_thread;

unsigned long refresh_display = 1 * NSEC_PER_SEC;
unsigned long last_display_update = 0;

/* list of FDs available for being read with snapshots */
struct bt_mmap_stream_list mmap_list;
GPtrArray *lttng_consumer_stream_array;
int sessiond_metadata, consumerd_metadata;
struct lttng_consumer_local_data *ctx = NULL;
/* list of snapshots currently not consumed */
GPtrArray *available_snapshots;
sem_t metadata_available;
int reload_trace = 0;

int last_textdump_print_newline = 1;

enum {
	OPT_NONE = 0,
	OPT_HELP,
	OPT_TEXTDUMP,
	OPT_PID,
	OPT_CHILD,
	OPT_HOSTNAME,
	OPT_RELAY_HOSTNAME,
	OPT_KPROBES,
};

static struct poptOption long_options[] = {
	/* longName, shortName, argInfo, argPtr, value, descrip, argDesc */
	{ "help", 'h', POPT_ARG_NONE, NULL, OPT_HELP, NULL, NULL },
	{ "textdump", 't', POPT_ARG_NONE, NULL, OPT_TEXTDUMP, NULL, NULL },
	{ "child", 'f', POPT_ARG_NONE, NULL, OPT_CHILD, NULL, NULL },
	{ "pid", 'p', POPT_ARG_STRING, &opt_tid, OPT_PID, NULL, NULL },
	{ "hostname", 'n', POPT_ARG_STRING, &opt_hostname, OPT_HOSTNAME, NULL, NULL },
	{ "relay-hostname", 'r', POPT_ARG_STRING, &opt_relay_hostname,
		OPT_RELAY_HOSTNAME, NULL, NULL },
	{ "kprobes", 'k', POPT_ARG_STRING, &opt_kprobes, OPT_KPROBES, NULL, NULL },
	{ NULL, 0, 0, NULL, 0, NULL, NULL },
};

static void handle_textdump_sigterm(int signal)
{
	quit = 1;
	lttng_destroy_session("test");
}

void *refresh_thread(void *p)
{
	while (1) {
		if (quit) {
			sem_post(&pause_sem);
			sem_post(&timer);
			sem_post(&end_trace_sem);
			sem_post(&goodtodisplay);
			sem_post(&goodtoupdate);
			pthread_exit(0);
		}
		if (!opt_input_path) {
#if LTTNGTOP_MMAP_LIVE 
			struct bt_mmap_stream *mmap_info;

			bt_list_for_each_entry(mmap_info, &mmap_list.head, list)
				helper_kernctl_buffer_flush(mmap_info->fd);
#endif
		}
		sem_wait(&pause_sem);
		sem_post(&pause_sem);
		sem_post(&timer);
		sleep(refresh_display/NSEC_PER_SEC);
	}
}

void *ncurses_display(void *p)
{
	unsigned int current_display_index = 0;

	sem_wait(&bootstrap);
	/*
	 * Prevent the 1 second delay when we hit ESC
	 */
	ESCDELAY = 0;
	init_ncurses();

	while (1) {
		sem_wait(&timer);
		sem_wait(&goodtodisplay);
		sem_wait(&pause_sem);

		if (quit) {
			sem_post(&pause_sem);
			sem_post(&timer);
			reset_ncurses();
			pthread_exit(0);
		}

		copy = g_ptr_array_index(copies, current_display_index);
		assert(copy);
		display(current_display_index++);

		sem_post(&goodtoupdate);
		sem_post(&pause_sem);
	}
}

void print_fields(struct bt_ctf_event *event)
{
	unsigned int cnt, i;
	const struct bt_definition *const * list;
	const struct bt_declaration *l;
	const struct bt_definition *scope;
	enum ctf_type_id type;
	const char *str;

	scope = bt_ctf_get_top_level_scope(event, BT_EVENT_FIELDS);

	bt_ctf_get_field_list(event, scope, &list, &cnt);
	for (i = 0; i < cnt; i++) {
		if (i != 0)
			printf(", ");
		printf("%s = ", bt_ctf_field_name(list[i]));
		l = bt_ctf_get_decl_from_def(list[i]);
		type = bt_ctf_field_type(l);
		if (type == CTF_TYPE_INTEGER) {
			if (bt_ctf_get_int_signedness(l) == 0)
				printf("%" PRIu64 "", bt_ctf_get_uint64(list[i]));
			else
				printf("%" PRId64 "", bt_ctf_get_int64(list[i]));
		} else if (type == CTF_TYPE_STRING) {
			printf("%s", bt_ctf_get_string(list[i]));
		} else if (type == CTF_TYPE_ARRAY) {
			str = bt_ctf_get_char_array(list[i]);
			if (!bt_ctf_field_get_error() && str)
				printf("%s", str);
		}
	}
}

/*
 * hook on each event to check the timestamp and refresh the display if
 * necessary
 */
enum bt_cb_ret print_timestamp(struct bt_ctf_event *call_data, void *private_data)
{
	unsigned long timestamp;
	struct tm start;
	uint64_t ts_nsec_start;
	int pid, cpu_id;
	int64_t syscall_ret;
	const struct bt_definition *scope;
	const char *hostname, *procname;

	timestamp = bt_ctf_get_timestamp(call_data);

	start = format_timestamp(timestamp);
	ts_nsec_start = timestamp % NSEC_PER_SEC;

	pid = get_context_pid(call_data);
	if (pid == -1ULL && opt_tid) {
		goto error;
	}
	
	hostname = get_context_hostname(call_data);
	if (opt_tid || opt_hostname)
		if (!lookup_filter_tid_list(pid))
			goto end;

	cpu_id = get_cpu_id(call_data);
	procname = get_context_comm(call_data);

	if ((strcmp(bt_ctf_event_name(call_data), "exit_syscall") == 0) &&
			!last_textdump_print_newline) {
		scope = bt_ctf_get_top_level_scope(call_data,
				BT_EVENT_FIELDS);
		syscall_ret = bt_ctf_get_int64(bt_ctf_get_field(call_data,
					scope, "_ret"));
		printf("= %ld\n", syscall_ret);
		last_textdump_print_newline = 1;
	} else {
		/* we might have lost the exit_syscall event, so need to
		 * print the newline in this case */
		if (last_textdump_print_newline == 0)
			printf("\n");
		printf("%02d:%02d:%02d.%09" PRIu64 " (%s) (cpu %d) [%s (%d)] %s (",
				start.tm_hour, start.tm_min, start.tm_sec,
				ts_nsec_start, hostname, cpu_id, procname, pid,
				bt_ctf_event_name(call_data));
		print_fields(call_data);
		printf(") ");
		if (strncmp(bt_ctf_event_name(call_data), "sys_", 4) != 0) {
			printf("\n");
			last_textdump_print_newline = 1;
		} else {
			last_textdump_print_newline = 0;
		}
	}

end:
	return BT_CB_OK;
error:
	return BT_CB_ERROR_STOP;
}

enum bt_cb_ret handle_kprobes(struct bt_ctf_event *call_data, void *private_data)
{
	int i;
	struct kprobes *kprobe;

	/* for kprobes */
	for (i = 0; i < lttngtop.kprobes_table->len; i++) {
		kprobe = g_ptr_array_index(lttngtop.kprobes_table, i);
		if (strcmp(bt_ctf_event_name(call_data), kprobe->probe_name) == 0) {
			kprobe->count++;
		}
	}

	return BT_CB_OK;
}

/*
 * hook on each event to check the timestamp and refresh the display if
 * necessary
 */
enum bt_cb_ret check_timestamp(struct bt_ctf_event *call_data, void *private_data)
{
	unsigned long timestamp;

	timestamp = bt_ctf_get_timestamp(call_data);
	if (timestamp == -1ULL)
		goto error;

	if (last_display_update == 0)
		last_display_update = timestamp;

	if (timestamp - last_display_update >= refresh_display) {
		sem_wait(&goodtoupdate);
		g_ptr_array_add(copies, get_copy_lttngtop(last_display_update,
					timestamp));
		sem_post(&goodtodisplay);
		sem_post(&bootstrap);
		last_display_update = timestamp;
	}
	return BT_CB_OK;

error:
	fprintf(stderr, "check_timestamp callback error\n");
	return BT_CB_ERROR_STOP;
}

/*
 * get_perf_counter : get or create and return a perf_counter struct for
 * either a process or a cpu (only one of the 2 parameters mandatory)
 */
struct perfcounter *get_perf_counter(const char *name, struct processtop *proc,
		struct cputime *cpu)
{
	struct perfcounter *ret;
	GHashTable *table;

	if (proc)
		table = proc->perf;
	else if (cpu)
		table = cpu->perf;
	else
		goto error;

	ret = g_hash_table_lookup(table, (gpointer) name);
	if (ret)
		goto end;

	ret = g_new0(struct perfcounter, 1);
	/* by default, make it visible in the UI */
	ret->visible = 1;
	g_hash_table_insert(table, (gpointer) strdup(name), ret);

end:
	return ret;

error:
	return NULL;
}

void update_perf_value(struct processtop *proc, struct cputime *cpu,
		const char *name, int value)
{
	struct perfcounter *cpu_perf, *process_perf;

	cpu_perf = get_perf_counter(name, NULL, cpu);
	if (cpu_perf->count < value) {
		process_perf = get_perf_counter(name, proc, NULL);
		process_perf->count += value - cpu_perf->count;
		cpu_perf->count = value;
	}
}

void extract_perf_counter_scope(const struct bt_ctf_event *event,
		const struct bt_definition *scope,
		struct processtop *proc,
		struct cputime *cpu)
{
	struct bt_definition const * const *list = NULL;
	const struct bt_definition *field;
	unsigned int count;
	struct perfcounter *perfcounter;
	GHashTableIter iter;
	gpointer key;
	int ret;

	if (!scope)
		goto end;

	ret = bt_ctf_get_field_list(event, scope, &list, &count);
	if (ret < 0)
		goto end;

	if (count == 0)
		goto end;

	g_hash_table_iter_init(&iter, global_perf_liszt);
	while (g_hash_table_iter_next (&iter, &key, (gpointer) &perfcounter)) {
		field = bt_ctf_get_field(event, scope, (char *) key);
		if (field) {
			int value = bt_ctf_get_uint64(field);
			if (bt_ctf_field_get_error())
				continue;
			update_perf_value(proc, cpu, (char *) key, value);
		}
	}

end:
	return;
}

void update_perf_counter(struct processtop *proc, const struct bt_ctf_event *event)
{
	struct cputime *cpu;
	const struct bt_definition *scope;

	cpu = get_cpu(get_cpu_id(event));

	scope = bt_ctf_get_top_level_scope(event, BT_STREAM_EVENT_CONTEXT);
	extract_perf_counter_scope(event, scope, proc, cpu);

	scope = bt_ctf_get_top_level_scope(event, BT_STREAM_PACKET_CONTEXT);
	extract_perf_counter_scope(event, scope, proc, cpu);

	scope = bt_ctf_get_top_level_scope(event, BT_EVENT_CONTEXT);
	extract_perf_counter_scope(event, scope, proc, cpu);
}

enum bt_cb_ret fix_process_table(struct bt_ctf_event *call_data,
		void *private_data)
{
	int pid, tid, ppid, vpid, vtid, vppid;
	char *comm, *hostname;
	struct processtop *parent, *child;
	unsigned long timestamp;

	timestamp = bt_ctf_get_timestamp(call_data);
	if (timestamp == -1ULL)
		goto error;

	pid = get_context_pid(call_data);
	if (pid == -1ULL) {
		goto error;
	}

	tid = get_context_tid(call_data);
	if (tid == -1ULL) {
		goto error;
	}
	ppid = get_context_ppid(call_data);
	if (ppid == -1ULL) {
		goto error;
	}
	vpid = get_context_vpid(call_data);
	if (pid == -1ULL) {
		vpid = -1;
	}
	vtid = get_context_vtid(call_data);
	if (tid == -1ULL) {
		vtid = -1;
	}
	vppid = get_context_vppid(call_data);
	if (ppid == -1ULL) {
		vppid = -1;
	}
	comm = get_context_comm(call_data);
	if (!comm) {
		goto error;
	}
	/* optional */
	hostname = get_context_hostname(call_data);

	/* find or create the current process */
	child = find_process_tid(&lttngtop, tid, comm);
	if (!child)
		child = add_proc(&lttngtop, tid, comm, timestamp, hostname);
	if (!child)
		goto end;
	update_proc(child, pid, tid, ppid, vpid, vtid, vppid, comm, hostname);

	if (pid != tid) {
		/* find or create the parent */
		parent = find_process_tid(&lttngtop, pid, comm);
		if (!parent) {
			parent = add_proc(&lttngtop, pid, comm, timestamp, hostname);
			if (parent)
				parent->pid = pid;
		}

		/* attach the parent to the current process */
		child->threadparent = parent;
		add_thread(parent, child);
	}

	update_perf_counter(child, call_data);

end:
	return BT_CB_OK;

error:
	return BT_CB_ERROR_STOP;
}

void init_lttngtop()
{
	copies = g_ptr_array_new();
	global_perf_liszt = g_hash_table_new(g_str_hash, g_str_equal);
	global_filter_list = g_hash_table_new(g_str_hash, g_str_equal);
	global_host_list = g_hash_table_new(g_str_hash, g_str_equal);

	sem_init(&goodtodisplay, 0, 0);
	sem_init(&goodtoupdate, 0, 1);
	sem_init(&timer, 0, 1);
	sem_init(&bootstrap, 0, 0);
	sem_init(&pause_sem, 0, 1);
	sem_init(&end_trace_sem, 0, 0);

	reset_global_counters();
	lttngtop.nbproc = 0;
	lttngtop.nbthreads = 0;
	lttngtop.nbfiles = 0;

	lttngtop.process_hash_table = g_hash_table_new(g_direct_hash,
			g_direct_equal);
	lttngtop.process_table = g_ptr_array_new();
	lttngtop.files_table = g_ptr_array_new();
	lttngtop.cpu_table = g_ptr_array_new();

	toggle_filter = -1;
}

void usage(FILE *fp)
{
	fprintf(fp, "LTTngTop %s\n\n", VERSION);
	fprintf(fp, "Usage : lttngtop [OPTIONS] [TRACE]\n");
	fprintf(fp, "  TRACE			Path to the trace to analyse (no trace path for live tracing)\n");
	fprintf(fp, "  -h, --help               This help message\n");
	fprintf(fp, "  -t, --textdump           Display live events in text-only\n");
	fprintf(fp, "  -p, --pid                Comma-separated list of PIDs to display\n");
	fprintf(fp, "  -f, --child              Follow threads associated with selected PIDs\n");
	fprintf(fp, "  -n, --hostname           Comma-separated list of hostnames to display (require hostname context in trace)\n");
	fprintf(fp, "  -k, --kprobes            Comma-separated list of kprobes to insert (same format as lttng enable-event)\n");
}

/*
 * Parse probe options.
 * Shamelessly stolen from lttng-tools :
 * src/bin/lttng/commands/enable_events.c
 */
static struct kprobes *parse_probe_opts(char *opt)
{
	char s_hex[19];
	char name[LTTNG_SYMBOL_NAME_LEN];
	struct kprobes *kprobe;
	int ret;

	/*
	kprobe->probe_addr = 0;
	kprobe->probe_offset = 0;
	asprintf(&kprobe->probe_name, "probe_sys_open");
	asprintf(&kprobe->symbol_name, "sys_open");
	*/

	if (opt == NULL) {
		kprobe = NULL;
		goto end;
	}

	kprobe = g_new0(struct kprobes, 1);

	/* Check for symbol+offset */
	ret = sscanf(opt, "%[^'+']+%s", name, s_hex);
	if (ret == 2) {
		ret = asprintf(&kprobe->probe_name, "probe_%s", name);
		ret = asprintf(&kprobe->symbol_name, "%s", name);

		if (strlen(s_hex) == 0) {
			fprintf(stderr, "Invalid probe offset %s", s_hex);
			ret = -1;
			goto end;
		}
		kprobe->probe_offset = strtoul(s_hex, NULL, 0);
		kprobe->probe_addr = 0;
		goto end;
	}

	/* Check for symbol */
	if (isalpha(name[0])) {
		ret = sscanf(opt, "%s", name);
		if (ret == 1) {
			ret = asprintf(&kprobe->probe_name, "probe_%s", name);
			ret = asprintf(&kprobe->symbol_name, "%s", name);
			kprobe->probe_offset = 0;
			kprobe->probe_addr = 0;
			goto end;
		}
	}

	/* Check for address */
	ret = sscanf(opt, "%s", s_hex);
	if (ret > 0) {
		if (strlen(s_hex) == 0) {
			fprintf(stderr, "Invalid probe address %s", s_hex);
			ret = -1;
			goto end;
		}
		ret = asprintf(&kprobe->probe_name, "probe_%s", s_hex);
		kprobe->probe_offset = 0;
		kprobe->probe_addr = strtoul(s_hex, NULL, 0);
		goto end;
	}

	/* No match */
	kprobe = NULL;

end:
	return kprobe;
}

/*
 * Return 0 if caller should continue, < 0 if caller should return
 * error, > 0 if caller should exit without reporting error.
 */
static int parse_options(int argc, char **argv)
{
	poptContext pc;
	int opt, ret = 0;
	char *tmp_str;
	int *tid;

	remote_live = 0;

	pc = poptGetContext(NULL, argc, (const char **) argv, long_options, 0);
	poptReadDefaultConfig(pc, 0);

	while ((opt = poptGetNextOpt(pc)) != -1) {
		switch (opt) {
			case OPT_HELP:
				usage(stdout);
				ret = 1;    /* exit cleanly */
				goto end;
			case OPT_TEXTDUMP:
				opt_textdump = 1;
				break;
			case OPT_CHILD:
				opt_textdump = 1;
				opt_child = 1;
				break;
			case OPT_PID:
				toggle_filter = 1;
				tid_filter_list = g_hash_table_new(g_str_hash,
						g_str_equal);
				tmp_str = strtok(opt_tid, ",");
				while (tmp_str) {
					tid = malloc(sizeof(int));
					*tid = atoi(tmp_str);
					g_hash_table_insert(tid_filter_list,
							(gpointer) tid, tid);
					tmp_str = strtok(NULL, ",");
				}
				break;
			case OPT_HOSTNAME:
				toggle_filter = 1;
				tmp_str = strtok(opt_hostname, ",");
				while (tmp_str) {
					add_hostname_list(tmp_str, 1);
					tmp_str = strtok(NULL, ",");
				}
				break;
			case OPT_RELAY_HOSTNAME:
				remote_live = 1;
				break;
			case OPT_KPROBES:
				lttngtop.kprobes_table = g_ptr_array_new();
				tmp_str = strtok(opt_kprobes, ",");
				while (tmp_str) {
					struct kprobes *kprobe;

					kprobe = parse_probe_opts(tmp_str);
					if (kprobe) {
						g_ptr_array_add(
							lttngtop.kprobes_table,
							kprobe);
					} else {
						ret = -EINVAL;
						goto end;
					}
					tmp_str = strtok(NULL, ",");
				}
				break;
			default:
				ret = -EINVAL;
				goto end;
		}
	}

	opt_input_path = poptGetArg(pc);

end:
	if (pc) {
		poptFreeContext(pc);
	}
	return ret;
}

void iter_trace(struct bt_context *bt_ctx)
{
	struct bt_ctf_iter *iter;
	struct bt_iter_pos begin_pos;
	struct kprobes *kprobe;
	const struct bt_ctf_event *event;
	int i;
	int ret = 0;

	begin_pos.type = BT_SEEK_BEGIN;
	iter = bt_ctf_iter_create(bt_ctx, &begin_pos, NULL);

	/* at each event, verify the status of the process table */
	bt_ctf_iter_add_callback(iter, 0, NULL, 0,
			fix_process_table,
			NULL, NULL, NULL);
	if (opt_textdump) {
		bt_ctf_iter_add_callback(iter, 0, NULL, 0,
				print_timestamp,
				NULL, NULL, NULL);
	} else {
		/* at each event check if we need to refresh */
		bt_ctf_iter_add_callback(iter, 0, NULL, 0,
				check_timestamp,
				NULL, NULL, NULL);
		/* to handle the scheduling events */
		bt_ctf_iter_add_callback(iter,
				g_quark_from_static_string("sched_switch"),
				NULL, 0, handle_sched_switch, NULL, NULL, NULL);
		/* to clean up the process table */
		bt_ctf_iter_add_callback(iter,
				g_quark_from_static_string("sched_process_free"),
				NULL, 0, handle_sched_process_free, NULL, NULL, NULL);
		/* to get all the process from the statedumps */
		bt_ctf_iter_add_callback(iter,
				g_quark_from_static_string(
					"lttng_statedump_process_state"),
				NULL, 0, handle_statedump_process_state,
				NULL, NULL, NULL);

		/* for IO top */
		bt_ctf_iter_add_callback(iter,
				g_quark_from_static_string("exit_syscall"),
				NULL, 0, handle_exit_syscall, NULL, NULL, NULL);
		bt_ctf_iter_add_callback(iter,
				g_quark_from_static_string("sys_write"),
				NULL, 0, handle_sys_write, NULL, NULL, NULL);
		bt_ctf_iter_add_callback(iter,
				g_quark_from_static_string("sys_read"),
				NULL, 0, handle_sys_read, NULL, NULL, NULL);
		bt_ctf_iter_add_callback(iter,
				g_quark_from_static_string("sys_open"),
				NULL, 0, handle_sys_open, NULL, NULL, NULL);
		bt_ctf_iter_add_callback(iter,
				g_quark_from_static_string("sys_close"),
				NULL, 0, handle_sys_close, NULL, NULL, NULL);
		bt_ctf_iter_add_callback(iter,
				g_quark_from_static_string(
					"lttng_statedump_file_descriptor"),
				NULL, 0, handle_statedump_file_descriptor,
				NULL, NULL, NULL);

		/* for kprobes */
		if (lttngtop.kprobes_table) {
			for (i = 0; i < lttngtop.kprobes_table->len; i++) {
				kprobe = g_ptr_array_index(lttngtop.kprobes_table, i);
				bt_ctf_iter_add_callback(iter,
						g_quark_from_static_string(
							kprobe->probe_name),
						NULL, 0, handle_kprobes,
						NULL, NULL, NULL);
			}
		}
	}

	while ((event = bt_ctf_iter_read_event(iter)) != NULL) {
		if (quit || reload_trace)
			goto end_iter;
		ret = bt_iter_next(bt_ctf_get_iter(iter));
		if (ret < 0)
			goto end_iter;
	}

	/* block until quit, we reached the end of the trace */
	sem_wait(&end_trace_sem);

end_iter:
	bt_ctf_iter_destroy(iter);
}

/*
 * bt_context_add_traces_recursive: Open a trace recursively
 * (copied from BSD code in converter/babeltrace.c)
 *
 * Find each trace present in the subdirectory starting from the given
 * path, and add them to the context. The packet_seek parameter can be
 * NULL: this specify to use the default format packet_seek.
 *
 * Return: 0 on success, nonzero on failure.
 * Unable to open toplevel: failure.
 * Unable to open some subdirectory or file: warn and continue;
 */
int bt_context_add_traces_recursive(struct bt_context *ctx, const char *path,
		const char *format_str,
		void (*packet_seek)(struct bt_stream_pos *pos,
			size_t offset, int whence))
{
	FTS *tree;
	FTSENT *node;
	GArray *trace_ids;
	char lpath[PATH_MAX];
	char * const paths[2] = { lpath, NULL };
	int ret = -1;

	/*
	 * Need to copy path, because fts_open can change it.
	 * It is the pointer array, not the strings, that are constant.
	 */
	strncpy(lpath, path, PATH_MAX);
	lpath[PATH_MAX - 1] = '\0';

	tree = fts_open(paths, FTS_NOCHDIR | FTS_LOGICAL, 0);
	if (tree == NULL) {
		fprintf(stderr, "[error] [Context] Cannot traverse \"%s\" for reading.\n",
				path);
		return -EINVAL;
	}

	trace_ids = g_array_new(FALSE, TRUE, sizeof(int));

	while ((node = fts_read(tree))) {
		int dirfd, metafd;

		if (!(node->fts_info & FTS_D))
			continue;

		dirfd = open(node->fts_accpath, 0);
		if (dirfd < 0) {
			fprintf(stderr, "[error] [Context] Unable to open trace "
				"directory file descriptor.\n");
			ret = dirfd;
			goto error;
		}
		metafd = openat(dirfd, "metadata", O_RDONLY);
		if (metafd < 0) {
			close(dirfd);
			ret = -1;
			continue;
		} else {
			int trace_id;

			ret = close(metafd);
			if (ret < 0) {
				perror("close");
				goto error;
			}
			ret = close(dirfd);
			if (ret < 0) {
				perror("close");
				goto error;
			}

			trace_id = bt_context_add_trace(ctx,
				node->fts_accpath, format_str,
				packet_seek, NULL, NULL);
			if (trace_id < 0) {
				fprintf(stderr, "[warning] [Context] opening trace \"%s\" from %s "
					"for reading.\n", node->fts_accpath, path);
				/* Allow to skip erroneous traces. */
				continue;
			}
			g_array_append_val(trace_ids, trace_id);
		}
	}

	g_array_free(trace_ids, TRUE);
	return ret;

error:
	return ret;
}

static int check_field_requirements(const struct bt_ctf_field_decl *const * field_list,
		int field_cnt, int *tid_check, int *pid_check,
		int *procname_check, int *ppid_check)
{
	int j;
	struct perfcounter *global;
	const char *name;

	for (j = 0; j < field_cnt; j++) {
		name = bt_ctf_get_decl_field_name(field_list[j]);
		if (*tid_check == 0) {
			if (strncmp(name, "tid", 3) == 0)
				(*tid_check)++;
		}
		if (*pid_check == 0) {
			if (strncmp(name, "pid", 3) == 0)
				(*pid_check)++;
		}
		if (*ppid_check == 0) {
			if (strncmp(name, "ppid", 4) == 0)
				(*ppid_check)++;
		}
		if (*procname_check == 0) {
			if (strncmp(name, "procname", 8) == 0)
				(*procname_check)++;
		}
		if (strncmp(name, "perf_", 5) == 0) {
			global = g_hash_table_lookup(global_perf_liszt, (gpointer) name);
			if (!global) {
				global = g_new0(struct perfcounter, 1);
				/* by default, sort on the first perf context */
				if (g_hash_table_size(global_perf_liszt) == 0)
					global->sort = 1;
				global->visible = 1;
				g_hash_table_insert(global_perf_liszt, (gpointer) strdup(name), global);
			}
		}
	}

	if (*tid_check == 1 && *pid_check == 1 && *ppid_check == 1 &&
			*procname_check == 1)
		return 0;

	return -1;
}

/*
 * check_requirements: check if the required context informations are available
 *
 * If each mandatory context information is available for at least in one
 * event, return 0 otherwise return -1.
 */
int check_requirements(struct bt_context *ctx)
{
	unsigned int i, evt_cnt, field_cnt;
	struct bt_ctf_event_decl *const * evt_list;
	const struct bt_ctf_field_decl *const * field_list;
	int tid_check = 0;
	int pid_check = 0;
	int procname_check = 0;
	int ppid_check = 0;
	int ret = 0;

	bt_ctf_get_event_decl_list(0, ctx, &evt_list, &evt_cnt);
	for (i = 0; i < evt_cnt; i++) {
		bt_ctf_get_decl_fields(evt_list[i], BT_STREAM_EVENT_CONTEXT,
				&field_list, &field_cnt);
		ret = check_field_requirements(field_list, field_cnt,
				&tid_check, &pid_check, &procname_check,
				&ppid_check);

		bt_ctf_get_decl_fields(evt_list[i], BT_EVENT_CONTEXT,
				&field_list, &field_cnt);
		ret = check_field_requirements(field_list, field_cnt,
				&tid_check, &pid_check, &procname_check,
				&ppid_check);

		bt_ctf_get_decl_fields(evt_list[i], BT_STREAM_PACKET_CONTEXT,
				&field_list, &field_cnt);
		ret = check_field_requirements(field_list, field_cnt,
				&tid_check, &pid_check, &procname_check,
				&ppid_check);
	}

	if (tid_check == 0) {
		ret = -1;
		fprintf(stderr, "[error] missing tid context information\n");
	}
	if (pid_check == 0) {
		ret = -1;
		fprintf(stderr, "[error] missing pid context information\n");
	}
	if (ppid_check == 0) {
		ret = -1;
		fprintf(stderr, "[error] missing ppid context information\n");
	}
	if (procname_check == 0) {
		ret = -1;
		fprintf(stderr, "[error] missing procname context information\n");
	}

	return ret;
}

#if LTTNGTOP_MMAP_LIVE
ssize_t read_subbuffer(struct lttng_consumer_stream *kconsumerd_fd,
		struct lttng_consumer_local_data *ctx)
{
	unsigned long len;
	int err;
	long ret = 0;
	int infd = helper_get_lttng_consumer_stream_wait_fd(kconsumerd_fd);

	if (helper_get_lttng_consumer_stream_output(kconsumerd_fd) == LTTNG_EVENT_SPLICE) {
		/* Get the next subbuffer */
		err = helper_kernctl_get_next_subbuf(infd);
		if (err != 0) {
			ret = errno;
			perror("Reserving sub buffer failed (everything is normal, "
					"it is due to concurrency)");
			goto end;
		}
		/* read the whole subbuffer */
		err = helper_kernctl_get_padded_subbuf_size(infd, &len);
		if (err != 0) {
			ret = errno;
			perror("Getting sub-buffer len failed.");
			goto end;
		}

		/* splice the subbuffer to the tracefile */
		ret = helper_lttng_consumer_on_read_subbuffer_splice(ctx, kconsumerd_fd, len, 0);
		if (ret < 0) {
			/*
			 * display the error but continue processing to try
			 * to release the subbuffer
			 */
			fprintf(stderr,"Error splicing to tracefile\n");
		}
		err = helper_kernctl_put_next_subbuf(infd);
		if (err != 0) {
			ret = errno;
			perror("Reserving sub buffer failed (everything is normal, "
					"it is due to concurrency)");
			goto end;
		}
		sem_post(&metadata_available);
	}

end:
	return 0;
}

int on_update_fd(int key, uint32_t state)
{
	/* let the lib handle the metadata FD */
	if (key == sessiond_metadata)
		return 0;
	return 1;
}

int on_recv_fd(struct lttng_consumer_stream *kconsumerd_fd)
{
	int ret;
	struct bt_mmap_stream *new_mmap_stream;

	/* Opening the tracefile in write mode */
	if (helper_get_lttng_consumer_stream_path_name(kconsumerd_fd) != NULL) {
		ret = open(helper_get_lttng_consumer_stream_path_name(kconsumerd_fd),
				O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU|S_IRWXG|S_IRWXO);
		if (ret < 0) {
			perror("open");
			goto end;
		}
		helper_set_lttng_consumer_stream_out_fd(kconsumerd_fd, ret);
	}

	if (helper_get_lttng_consumer_stream_output(kconsumerd_fd) == LTTNG_EVENT_MMAP) {
		new_mmap_stream = malloc(sizeof(struct bt_mmap_stream));
		new_mmap_stream->fd = helper_get_lttng_consumer_stream_wait_fd(
				kconsumerd_fd);
		bt_list_add(&new_mmap_stream->list, &mmap_list.head);

		g_ptr_array_add(lttng_consumer_stream_array, kconsumerd_fd);
		/* keep mmap FDs internally */
		ret = 1;
	} else {
		consumerd_metadata = helper_get_lttng_consumer_stream_wait_fd(kconsumerd_fd);
		sessiond_metadata = helper_get_lttng_consumer_stream_key(kconsumerd_fd);
		ret = 0;
	}

	reload_trace = 1;

end:
	return ret;
}

void live_consume(struct bt_context **bt_ctx)
{
	int ret;
	FILE *metadata_fp;

	sem_wait(&metadata_available);
	if (access("/tmp/livesession/kernel/metadata", F_OK) != 0) {
		fprintf(stderr,"no metadata\n");
		goto end;
	}
	metadata_fp = fopen("/tmp/livesession/kernel/metadata", "r");

	*bt_ctx = bt_context_create();
	ret = bt_context_add_trace(*bt_ctx, NULL, "ctf",
			lttngtop_ctf_packet_seek, &mmap_list, metadata_fp);
	if (ret < 0) {
		printf("Error adding trace\n");
		goto end;
	}

end:
	return;
}

int setup_consumer(char *command_sock_path, pthread_t *threads,
		struct lttng_consumer_local_data *ctx)
{
	int ret = 0;

	ctx = helper_lttng_consumer_create(HELPER_LTTNG_CONSUMER_KERNEL,
		read_subbuffer, NULL, on_recv_fd, on_update_fd);
	if (!ctx)
		goto end;

	unlink(command_sock_path);
	helper_lttng_consumer_set_command_sock_path(ctx, command_sock_path);
	helper_lttng_consumer_init();

	/* Create the thread to manage the receive of fd */
	ret = pthread_create(&threads[0], NULL, helper_lttng_consumer_thread_sessiond_poll,
			(void *) ctx);
	if (ret != 0) {
		perror("pthread_create receive fd");
		goto end;
	}
	/* Create thread to manage the polling/writing of traces */
	ret = pthread_create(&threads[1], NULL, helper_lttng_consumer_thread_metadata_poll,
			(void *) ctx);
	if (ret != 0) {
		perror("pthread_create poll fd");
		goto end;
	}

end:
	return ret;
}

int enable_kprobes(struct lttng_handle *handle, char *channel_name)
{
	struct lttng_event ev;
	struct kprobes *kprobe;
	int ret = 0;
	int i;

	for (i = 0; i < lttngtop.kprobes_table->len; i++) {
		kprobe = g_ptr_array_index(lttngtop.kprobes_table, i);

		memset(&ev, '\0', sizeof(struct lttng_event));
		ev.type = LTTNG_EVENT_PROBE;
		if (kprobe->symbol_name)
			sprintf(ev.attr.probe.symbol_name, "%s", kprobe->symbol_name);
		sprintf(ev.name, "%s", kprobe->probe_name);
		ev.attr.probe.addr = kprobe->probe_addr;
		ev.attr.probe.offset = kprobe->probe_offset;
		if ((ret = lttng_enable_event(handle, &ev, channel_name)) < 0) {
			fprintf(stderr,"error enabling kprobes : %s\n",
					helper_lttcomm_get_readable_code(ret));
			goto end;
		}
	}

end:
	return ret;
}

int setup_live_tracing()
{
	struct lttng_domain dom;
	struct lttng_channel chan;
	char *channel_name = "mmapchan";
	struct lttng_event ev;
	int ret = 0;
	char *command_sock_path = "/tmp/consumerd_sock";
	static pthread_t threads[2]; /* recv_fd, poll */
	struct lttng_event_context kctxpid, kctxcomm, kctxppid, kctxtid,
				   kctxperf1, kctxperf2;

	struct lttng_handle *handle;

	BT_INIT_LIST_HEAD(&mmap_list.head);

	lttng_consumer_stream_array = g_ptr_array_new();

	if ((ret = setup_consumer(command_sock_path, threads, ctx)) < 0) {
		fprintf(stderr,"error setting up consumer\n");
		goto error;
	}

	available_snapshots = g_ptr_array_new();

	/* setup the session */
	dom.type = LTTNG_DOMAIN_KERNEL;

	ret = unlink("/tmp/livesession");

	lttng_destroy_session("test");
	if ((ret = lttng_create_session("test", "/tmp/livesession")) < 0) {
		fprintf(stderr,"error creating the session : %s\n",
				helper_lttcomm_get_readable_code(ret));
		goto error;
	}

	if ((handle = lttng_create_handle("test", &dom)) == NULL) {
		fprintf(stderr,"error creating handle\n");
		goto error_session;
	}

	/*
	 * FIXME : need to let the
	 * helper_lttng_consumer_thread_receive_fds create the
	 * socket.
	 * Cleaner solution ?
	 */
	while (access(command_sock_path, F_OK)) {
		sleep(0.1);
	}

	if ((ret = lttng_register_consumer(handle, command_sock_path)) < 0) {
		fprintf(stderr,"error registering consumer : %s\n",
				helper_lttcomm_get_readable_code(ret));
		goto error_session;
	}

	strcpy(chan.name, channel_name);
	chan.attr.overwrite = 0;
	if (opt_tid && opt_textdump) {
		chan.attr.subbuf_size = 32768;
		chan.attr.num_subbuf = 8;
	} else {
		//chan.attr.subbuf_size = 1048576; /* 1MB */
		chan.attr.subbuf_size = 2097152; /* 1MB */
		chan.attr.num_subbuf = 4;
	}
	chan.attr.switch_timer_interval = 0;
	chan.attr.read_timer_interval = 200;
	chan.attr.output = LTTNG_EVENT_MMAP;

	if ((ret = lttng_enable_channel(handle, &chan)) < 0) {
		fprintf(stderr,"error creating channel : %s\n",
				helper_lttcomm_get_readable_code(ret));
		goto error_session;
	}

	memset(&ev, '\0', sizeof(struct lttng_event));
	ev.type = LTTNG_EVENT_TRACEPOINT;
	sprintf(ev.name, "sched_switch");
	if ((ret = lttng_enable_event(handle, &ev, channel_name)) < 0) {
		fprintf(stderr,"error enabling event %s : %s\n",
				ev.name,
				helper_lttcomm_get_readable_code(ret));
		goto error_session;
	}
	sprintf(ev.name, "sched_process_free");
	if ((ret = lttng_enable_event(handle, &ev, channel_name)) < 0) {
		fprintf(stderr,"error enabling event %s : %s\n",
				ev.name,
				helper_lttcomm_get_readable_code(ret));
		goto error_session;
	}
	sprintf(ev.name, "lttng_statedump_process_state");
	if ((ret = lttng_enable_event(handle, &ev, channel_name)) < 0) {
		fprintf(stderr,"error enabling event %s : %s\n",
				ev.name,
				helper_lttcomm_get_readable_code(ret));
		goto error_session;
	}
	sprintf(ev.name, "lttng_statedump_file_descriptor");
	if ((ret = lttng_enable_event(handle, &ev, channel_name)) < 0) {
		fprintf(stderr,"error enabling event %s : %s\n",
				ev.name,
				helper_lttcomm_get_readable_code(ret));
		goto error_session;
	}

	memset(&ev, '\0', sizeof(struct lttng_event));
	ev.type = LTTNG_EVENT_SYSCALL;
	if ((ret = lttng_enable_event(handle, &ev, channel_name)) < 0) {
		fprintf(stderr,"error enabling syscalls : %s\n",
				helper_lttcomm_get_readable_code(ret));
		goto error_session;
	}

	if (lttngtop.kprobes_table) {
		ret = enable_kprobes(handle, channel_name);
		if (ret < 0) {
			goto error_session;
		}
	}

	kctxperf1.ctx = LTTNG_EVENT_CONTEXT_PERF_COUNTER;
	kctxperf1.u.perf_counter.type = 0; /* PERF_TYPE_HARDWARE */
	kctxperf1.u.perf_counter.config = 5; /* PERF_COUNT_HW_BRANCH_MISSES */
	sprintf(kctxperf1.u.perf_counter.name, "perf_branch_misses");
	ret = lttng_add_context(handle, &kctxperf1, NULL, NULL);
	if (ret < 0) {
		fprintf(stderr, "error enabling context %s\n",
				kctxtid.u.perf_counter.name);
	}

	kctxperf2.ctx = LTTNG_EVENT_CONTEXT_PERF_COUNTER;
	kctxperf2.u.perf_counter.type = 1; /* PERF_TYPE_SOFTWARE */
	kctxperf2.u.perf_counter.config = 6; /* PERF_COUNT_SW_PAGE_FAULTS_MAJ */
	sprintf(kctxperf2.u.perf_counter.name, "perf_major_faults");
	ret = lttng_add_context(handle, &kctxperf2, NULL, NULL);
	if (ret < 0) {
		fprintf(stderr, "error enabling context %s\n",
				kctxtid.u.perf_counter.name);
	}

	kctxpid.ctx = LTTNG_EVENT_CONTEXT_PID;
	lttng_add_context(handle, &kctxpid, NULL, NULL);
	kctxtid.ctx = LTTNG_EVENT_CONTEXT_TID;
	lttng_add_context(handle, &kctxtid, NULL, NULL);
	kctxppid.ctx = LTTNG_EVENT_CONTEXT_PPID;
	lttng_add_context(handle, &kctxppid, NULL, NULL);
	kctxcomm.ctx = LTTNG_EVENT_CONTEXT_PROCNAME;
	lttng_add_context(handle, &kctxcomm, NULL, NULL);
	kctxpid.ctx = LTTNG_EVENT_CONTEXT_VPID;
	lttng_add_context(handle, &kctxpid, NULL, NULL);
	kctxtid.ctx = LTTNG_EVENT_CONTEXT_VTID;
	lttng_add_context(handle, &kctxtid, NULL, NULL);
	kctxtid.ctx = LTTNG_EVENT_CONTEXT_HOSTNAME;
	lttng_add_context(handle, &kctxtid, NULL, NULL);


	if ((ret = lttng_start_tracing("test")) < 0) {
		fprintf(stderr,"error starting tracing : %s\n",
				helper_lttcomm_get_readable_code(ret));
		goto error_session;
	}

	helper_kernctl_buffer_flush(consumerd_metadata);

	/* block until metadata is ready */
	sem_init(&metadata_available, 0, 0);

	return 0;

error_session:
	lttng_destroy_session("test");
error:
	return -1;
}
#endif

int main(int argc, char **argv)
{
	int ret;
	struct bt_context *bt_ctx = NULL;

	init_lttngtop();
	ret = parse_options(argc, argv);
	if (ret < 0) {
		fprintf(stdout, "Error parsing options.\n\n");
		usage(stdout);
		exit(EXIT_FAILURE);
	} else if (ret > 0) {
		exit(EXIT_SUCCESS);
	}

	if (!opt_input_path) {
		if (opt_textdump) {
			signal(SIGTERM, handle_textdump_sigterm);
			signal(SIGINT, handle_textdump_sigterm);
		}
		if (remote_live) {

		}
#if LTTNGTOP_MMAP_LIVE
		struct bt_mmap_stream *mmap_info;

		ret = setup_live_tracing();
		if (ret < 0) {
			goto end;
		}
#endif

		if (!opt_textdump) {
			pthread_create(&display_thread, NULL, ncurses_display, (void *) NULL);
			pthread_create(&timer_thread, NULL, refresh_thread, (void *) NULL);
		}
#if LTTNGTOP_MMAP_LIVE
		while (!quit) {
			reload_trace = 0;
			live_consume(&bt_ctx);
			ret = check_requirements(bt_ctx);
			if (ret < 0) {
				fprintf(stderr, "[error] some mandatory contexts were missing, exiting.\n");
				goto end;
			}
			iter_trace(bt_ctx);
			/*
			 * FIXME : pb with cleanup in libbabeltrace
			ret = bt_context_remove_trace(bt_ctx, 0);
			if (ret != 0) {
				fprintf(stderr, "error removing trace\n");
				goto error;
			}
			*/
			if (bt_ctx) {
				bt_context_put(bt_ctx);
			}

			/*
			 * since we receive all FDs every time there is an
			 * update and the FD number is different every time,
			 * we don't know which one are valid.
			 * so we check if all FDs are usable with a simple
			 * ioctl call.
			 */
			bt_list_for_each_entry(mmap_info, &mmap_list.head, list) {
				unsigned long mmap_len;

				ret = helper_kernctl_get_mmap_len(mmap_info->fd, &mmap_len);
				if (ret != 0) {
					bt_list_del(&mmap_info->list);
				}
			}
			sem_post(&metadata_available);
		}
#endif

		pthread_join(timer_thread, NULL);
		quit = 1;
		pthread_join(display_thread, NULL);

		lttng_stop_tracing("test");
		lttng_destroy_session("test");

		goto end;
	} else {
		//init_lttngtop();

		bt_ctx = bt_context_create();
		ret = bt_context_add_traces_recursive(bt_ctx, opt_input_path, "ctf", NULL);
		if (ret < 0) {
			fprintf(stderr, "[error] Opening the trace\n");
			goto end;
		}

		ret = check_requirements(bt_ctx);
		if (ret < 0) {
			fprintf(stderr, "[error] some mandatory contexts were missing, exiting.\n");
			goto end;
		}
		pthread_create(&display_thread, NULL, ncurses_display, (void *) NULL);
		pthread_create(&timer_thread, NULL, refresh_thread, (void *) NULL);

		iter_trace(bt_ctx);

		pthread_join(display_thread, NULL);
		quit = 1;
		pthread_join(timer_thread, NULL);
	}

end:
	if (bt_ctx)
		bt_context_put(bt_ctx);

	return 0;
}
