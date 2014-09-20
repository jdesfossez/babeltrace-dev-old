#ifndef _LTTNG_STATE_TRACK_H
#define _LTTNG_STATE_TRACK_H

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

#include <libmemcached/memcached.h>

#include <babeltrace/types.h>
#include <babeltrace/ctf-ir/metadata.h>

int lttng_state_init(struct lttng_state_ctx *ctx);
int lttng_state_process_event(struct lttng_state_ctx *ctx,
	struct bt_stream_pos *ppos, struct ctf_stream_definition *stream);

#endif /* LTTNG_STATE_TRACK_H */
