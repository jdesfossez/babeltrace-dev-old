/*
 * Copyright (C) 2011 - Julien Desfossez <julien.desfossez@polymtl.ca>
 *                      Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; only version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef _LTTNG_KERNEL_CTL_H
#define _LTTNG_KERNEL_CTL_H

#include <lttng/lttng.h>
#include <common/lttng-kernel.h>

int kernctl_create_session(int fd);
int kernctl_open_metadata(int fd, struct lttng_channel_attr *chops);
int kernctl_create_channel(int fd, struct lttng_channel_attr *chops);
int kernctl_create_stream(int fd);
int kernctl_create_event(int fd, struct lttng_kernel_event *ev);
int kernctl_add_context(int fd, struct lttng_kernel_context *ctx);

int kernctl_enable(int fd);
int kernctl_disable(int fd);
int kernctl_start_session(int fd);
int kernctl_stop_session(int fd);

int kernctl_tracepoint_list(int fd);
int kernctl_tracer_version(int fd, struct lttng_kernel_tracer_version *v);
int kernctl_wait_quiescent(int fd);
int kernctl_calibrate(int fd, struct lttng_kernel_calibrate *calibrate);


/* Buffer operations */

/* For mmap mode, readable without "get" operation */
int kernctl_get_mmap_len(int fd, unsigned long *len);
int kernctl_get_max_subbuf_size(int fd, unsigned long *len);

/*
 * For mmap mode, operate on the current packet (between get/put or
 * get_next/put_next).
 */
int kernctl_get_mmap_read_offset(int fd, unsigned long *len);
int kernctl_get_subbuf_size(int fd, unsigned long *len);
int kernctl_get_padded_subbuf_size(int fd, unsigned long *len);

int kernctl_get_next_subbuf(int fd);
int kernctl_put_next_subbuf(int fd);

/* snapshot */
int kernctl_snapshot(int fd);
int kernctl_snapshot_get_consumed(int fd, unsigned long *pos);
int kernctl_snapshot_get_produced(int fd, unsigned long *pos);
int kernctl_get_subbuf(int fd, unsigned long *pos);
int kernctl_put_subbuf(int fd);

int kernctl_buffer_flush(int fd);

#endif /* _LTTNG_KERNEL_CTL_H */
