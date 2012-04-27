/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 04/12/2011
 * Adapted for LNet: 04/11/2012
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/assert.h"
#include "lib/chan.h"
#include "lib/cond.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h" /* C2_SET0 */
#include "net/net.h"
#include "net/lnet/lnet.h"
#include "net/lnet/st/ping.h"

#define DEF_RESPONSE "active pong"
#define DEF_SEND "passive ping"
#define SEND_RESP    " pong"
/** Descriptor for the tlist of buffers. */

enum {
	SEND_RETRIES = 3,
	IDBUF_LEN = C2_NET_LNET_XEP_ADDR_LEN + 16,
};

struct ping_work_item {
	enum c2_net_queue_type      pwi_type;
	struct c2_net_buffer       *pwi_nb;
	struct c2_list_link         pwi_link;
};

static struct c2_mutex qstats_mutex;
static struct c2_net_qstats ping_qs_total[C2_NET_QT_NR];
static uint64_t ping_qs_total_errors;
static uint64_t ping_qs_total_retries;

static void ping_print_qstats(struct nlx_ping_ctx *ctx,
			      struct c2_net_qstats *qp,
			      bool accumulate)

{
	int i;
	uint64_t hr;
	uint64_t min;
	uint64_t sec;
	uint64_t msec;
	static const char *qnames[C2_NET_QT_NR] = {
		"mRECV", "mSEND",
		"pRECV", "pSEND",
		"aRECV", "aSEND",
	};
	char tbuf[16];
	const char *lfmt =
"%5s %6lu %6lu %6lu %6lu %13s %14lu %13lu\n";
	const char *hfmt =
"Queue   #Add   #Del  #Succ  #Fail Time in Queue   Total Bytes  "
" Max Buffer Sz\n"
"----- ------ ------ ------ ------ ------------- ---------------"
" -------------\n";

	c2_mutex_lock(&qstats_mutex);
	ctx->pc_ops->pf("%s statistics:\n", ctx->pc_ident);
	ctx->pc_ops->pf("%s", hfmt);
	for (i = 0; i < C2_NET_QT_NR; ++qp, ++i) {
		sec = c2_time_seconds(qp->nqs_time_in_queue);
		hr = sec / SEC_PER_HR;
		min = sec % SEC_PER_HR / SEC_PER_MIN;
		sec %= SEC_PER_MIN;
		msec = (c2_time_nanoseconds(qp->nqs_time_in_queue) +
			ONE_MILLION / 2) / ONE_MILLION;
		sprintf(tbuf, "%02lu:%02lu:%02lu.%03lu",
			(long unsigned int) hr,
			(long unsigned int) min,
			(long unsigned int) sec,
			(long unsigned int) msec);
		ctx->pc_ops->pf(lfmt,
				qnames[i],
				qp->nqs_num_adds, qp->nqs_num_dels,
				qp->nqs_num_s_events, qp->nqs_num_f_events,
				tbuf, qp->nqs_total_bytes, qp->nqs_max_bytes);
		if (accumulate) {
			struct c2_net_qstats *cqp = &ping_qs_total[i];

#define PING_QSTATS_CLIENT_TOTAL(f) cqp->nqs_##f += qp->nqs_##f
			PING_QSTATS_CLIENT_TOTAL(time_in_queue);
			PING_QSTATS_CLIENT_TOTAL(num_adds);
			PING_QSTATS_CLIENT_TOTAL(num_dels);
			PING_QSTATS_CLIENT_TOTAL(num_s_events);
			PING_QSTATS_CLIENT_TOTAL(num_f_events);
			PING_QSTATS_CLIENT_TOTAL(total_bytes);
#undef PING_QSTATS_CLIENT_TOTAL
			cqp->nqs_max_bytes =
				max64u(cqp->nqs_max_bytes, qp->nqs_max_bytes);
		}
	}
	if (ctx->pc_sync_events) {
		ctx->pc_ops->pf("%s Loops: Work=%lu Blocked=%lu\n",
				ctx->pc_ident,
				(unsigned long) ctx->pc_worked_count,
				(unsigned long) ctx->pc_blocked_count);
		ctx->pc_ops->pf("%s Wakeups: WorkQ=%lu Net=%lu\n",
				ctx->pc_ident,
				(unsigned long) ctx->pc_wq_signal_count,
				(unsigned long) ctx->pc_net_signal_count);
	}
	ctx->pc_ops->pf("%s errors: %lu\n", ctx->pc_ident,
			(long unsigned int)c2_atomic64_get(&ctx->pc_errors));
	ctx->pc_ops->pf("%s retries: %lu\n", ctx->pc_ident,
			(long unsigned int)c2_atomic64_get(&ctx->pc_retries));
	if (accumulate) {
		ping_qs_total_errors += c2_atomic64_get(&ctx->pc_errors);
		ping_qs_total_retries += c2_atomic64_get(&ctx->pc_retries);
	}

	c2_mutex_unlock(&qstats_mutex);
}

void nlx_ping_print_qstats_tm(struct nlx_ping_ctx *ctx, bool reset)
{
	struct c2_net_qstats qs[C2_NET_QT_NR];
	bool is_client;
	int rc;

	if (ctx->pc_tm.ntm_state < C2_NET_TM_INITIALIZED)
		return;
	is_client = ctx->pc_ident[0] == 'C';
	rc = c2_net_tm_stats_get(&ctx->pc_tm, C2_NET_QT_NR, qs, reset);
	C2_ASSERT(rc == 0);
	ping_print_qstats(ctx, qs, is_client);
}


void nlx_ping_print_qstats_total(const char *ident,
				 const struct nlx_ping_ops *ops)
{
	struct nlx_ping_ctx tctx = {
		.pc_ops    = ops,
		.pc_ident  = ident,
	};
	c2_atomic64_set(&tctx.pc_errors, ping_qs_total_errors);
	c2_atomic64_set(&tctx.pc_retries, ping_qs_total_retries);
	ping_print_qstats(&tctx, ping_qs_total, false);
}

static void ping_sleep_secs(int secs)
{
	c2_time_t req, rem;
	if (secs == 0)
		return;
	c2_time_set(&req, secs, 0);
	c2_nanosleep(req, &rem);
}

static c2_time_t ping_c2_time_after_secs(int secs)
{
	c2_time_t dur;
	c2_time_set(&dur, secs, 0);
	return c2_time_add(c2_time_now(), dur);
}

static int alloc_buffers(int num, uint32_t segs, c2_bcount_t segsize,
			 unsigned shift, struct c2_net_buffer **out)
{
	struct c2_net_buffer *nbs;
	struct c2_net_buffer *nb;
	int                   i;
	int                   rc = 0;

	C2_ALLOC_ARR(nbs, num);
	if (nbs == NULL)
		return -ENOMEM;
	for (i = 0; i < num; ++i) {
		nb = &nbs[i];
		rc = c2_bufvec_alloc_aligned(&nb->nb_buffer, segs, segsize,
					     shift);
		if (rc != 0)
			break;
	}

	if (rc == 0)
		*out = nbs;
	else {
		while (--i >= 0)
			c2_bufvec_free_aligned(&nbs[i].nb_buffer, shift);
		c2_free(nbs);
	}
	return rc;
}

/**
   Get a unused buffer from the context buffer list.
   On success, marks the buffer as in-use and returns it.
   @retval ptr the buffer
   @retval NULL failure
 */
static struct c2_net_buffer *ping_buf_get(struct nlx_ping_ctx *ctx)
{
	int i;
	struct c2_net_buffer *nb;

	c2_mutex_lock(&ctx->pc_mutex);
	for (i = 0; i < ctx->pc_nr_bufs; ++i)
		if (!c2_bitmap_get(&ctx->pc_nbbm, i)) {
			c2_bitmap_set(&ctx->pc_nbbm, i, true);
			break;
		}
	c2_mutex_unlock(&ctx->pc_mutex);

	if (i == ctx->pc_nr_bufs)
		return NULL;

	nb = &ctx->pc_nbs[i];
	C2_ASSERT(nb->nb_flags == C2_NET_BUF_REGISTERED);
	return nb;
}

/**
   Releases a buffer back to the free buffer pool.
   The buffer is marked as not in-use.
 */
static void ping_buf_put(struct nlx_ping_ctx *ctx, struct c2_net_buffer *nb)
{
	int i = nb - &ctx->pc_nbs[0];
	C2_ASSERT(i >= 0 && i < ctx->pc_nr_bufs);
	C2_ASSERT((nb->nb_flags & ~C2_NET_BUF_REGISTERED) == 0);

	c2_mutex_lock(&ctx->pc_mutex);
	C2_ASSERT(c2_bitmap_get(&ctx->pc_nbbm, i));
	c2_bitmap_set(&ctx->pc_nbbm, i, false);
	c2_mutex_unlock(&ctx->pc_mutex);
}

/** encode a string message into a net buffer, not zero-copy */
static int encode_msg(struct c2_net_buffer *nb, const char *str)
{
	char *bp;
	c2_bcount_t len = strlen(str) + 1; /* include trailing nul */
	c2_bcount_t copied;
	struct c2_bufvec in = C2_BUFVEC_INIT_BUF((void **) &str, &len);
	struct c2_bufvec_cursor incur;
	struct c2_bufvec_cursor cur;

	nb->nb_length = len + 1;
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	bp = c2_bufvec_cursor_addr(&cur);
	*bp = 'm';
	C2_ASSERT(!c2_bufvec_cursor_move(&cur, 1));
	c2_bufvec_cursor_init(&incur, &in);
	copied = c2_bufvec_cursor_copy(&cur, &incur, len);
	C2_ASSERT(copied == len);
	return 0;
}

/** encode a descriptor into a net buffer, not zero-copy */
static int encode_desc(struct c2_net_buffer *nb,
		       bool send_desc,
		       const struct c2_net_buf_desc *desc)
{
	struct c2_bufvec_cursor cur;
	char *bp;
	c2_bcount_t step;

	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	bp = c2_bufvec_cursor_addr(&cur);
	*bp = send_desc ? 's' : 'r';
	C2_ASSERT(!c2_bufvec_cursor_move(&cur, 1));
	bp = c2_bufvec_cursor_addr(&cur);

	/* only support sending net_desc in single chunks in this test */
	step = c2_bufvec_cursor_step(&cur);
	C2_ASSERT(step >= 9 + desc->nbd_len);
	nb->nb_length = 10 + desc->nbd_len;

	bp += sprintf(bp, "%08d", desc->nbd_len);
	++bp;				/* +nul */
	memcpy(bp, desc->nbd_data, desc->nbd_len);
	return 0;
}

enum ping_msg_type {
	/** client wants to do passive send, server will active recv */
	PM_SEND_DESC,
	/** client wants to do active send, server will passive recv */
	PM_RECV_DESC,
	PM_MSG
};

struct ping_msg {
	enum ping_msg_type pm_type;
	union {
		char *pm_str;
		struct c2_net_buf_desc pm_desc;
	} pm_u;
};

/** decode a net buffer, allocates memory and copies payload */
static int decode_msg(struct c2_net_buffer *nb,
		      c2_bcount_t nb_len,
		      c2_bcount_t nb_offset,
		      struct ping_msg *msg)
{
	struct c2_bufvec_cursor cur;
	char *bp;
	c2_bcount_t step;

	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	if (nb_offset > 0)
		c2_bufvec_cursor_move(&cur, nb_offset);
	bp = c2_bufvec_cursor_addr(&cur);
	C2_ASSERT(*bp == 'm' || *bp == 's' || *bp == 'r');
	C2_ASSERT(!c2_bufvec_cursor_move(&cur, 1));
	if (*bp == 'm') {
		c2_bcount_t len = nb_len - 1;
		void *str;
		struct c2_bufvec out = C2_BUFVEC_INIT_BUF(&str, &len);
		struct c2_bufvec_cursor outcur;

		msg->pm_type = PM_MSG;
		str = msg->pm_u.pm_str = c2_alloc(len + 1);
		C2_ASSERT(str != NULL);
		c2_bufvec_cursor_init(&outcur, &out);
		step = c2_bufvec_cursor_copy(&outcur, &cur, len);
		C2_ASSERT(step == len);
	} else {
		int len;
		char nine[9];
		int i;
		void *buf;
		c2_bcount_t buflen;
		struct c2_bufvec bv = C2_BUFVEC_INIT_BUF(&buf, &buflen);
		struct c2_bufvec_cursor bv_cur;

		msg->pm_type = (*bp == 's') ? PM_SEND_DESC : PM_RECV_DESC;

		buf = nine;
		buflen = 9;
		c2_bufvec_cursor_init(&bv_cur, &bv);
		i = c2_bufvec_cursor_copy(&bv_cur, &cur, 9);
		C2_ASSERT(i == 9);
		i = sscanf(nine, "%d", &len);
		C2_ASSERT(i == 1);

		buflen = len;
		msg->pm_u.pm_desc.nbd_len = len;
		msg->pm_u.pm_desc.nbd_data = buf = c2_alloc(len);
		C2_ASSERT(buf != NULL);
		c2_bufvec_cursor_init(&bv_cur, &bv);
		i = c2_bufvec_cursor_copy(&bv_cur, &cur, len);
		C2_ASSERT(i == len);
	}
	return 0;
}

static void msg_free(struct ping_msg *msg)
{
	if (msg->pm_type != PM_MSG)
		c2_net_desc_free(&msg->pm_u.pm_desc);
	else
		c2_free(msg->pm_u.pm_str);
}

static void ping_print_interfaces(struct nlx_ping_ctx *ctx)
{
	int i;
	ctx->pc_ops->pf("%s: Available interfaces\n", ctx->pc_ident);
	for (i = 0; ctx->pc_interfaces[i] != NULL; ++i)
		ctx->pc_ops->pf("\t%s\n", ctx->pc_interfaces[i]);
	return;
}

static struct nlx_ping_ctx *
buffer_event_to_ping_ctx(const struct c2_net_buffer_event *ev)
{
	struct c2_net_buffer *nb = ev->nbe_buffer;
	C2_ASSERT(nb != NULL);
	return container_of(nb->nb_tm, struct nlx_ping_ctx, pc_tm);
}

/* client callbacks */
static void c_m_recv_cb(const struct c2_net_buffer_event *ev)
{
	struct nlx_ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	int rc;
	int len;
	struct ping_work_item *wi;
	struct ping_msg msg;

	C2_ASSERT(ev->nbe_buffer->nb_qtype == C2_NET_QT_MSG_RECV);
	PING_OUT(ctx, 1, "%s: Msg Recv CB\n", ctx->pc_ident);

	if (ev->nbe_status < 0) {
		if (ev->nbe_status == -ECANCELED)
			PING_OUT(ctx, 1, "%s: msg recv canceled\n",
				 ctx->pc_ident);
		else {
			ctx->pc_ops->pf("%s: msg recv error: %d\n",
					ctx->pc_ident, ev->nbe_status);
			c2_atomic64_inc(&ctx->pc_errors);
		}
	} else {
		ev->nbe_buffer->nb_length = ev->nbe_length;
		C2_ASSERT(ev->nbe_offset == 0);
		rc = decode_msg(ev->nbe_buffer, ev->nbe_length, 0, &msg);
		C2_ASSERT(rc == 0);

		if (msg.pm_type != PM_MSG)
			C2_IMPOSSIBLE("Client: got desc\n");

		len = strlen(msg.pm_u.pm_str);
		if (strlen(msg.pm_u.pm_str) < 32)
			PING_OUT(ctx, 1, "%s: got msg: %s\n",
				 ctx->pc_ident, msg.pm_u.pm_str);
		else
			PING_OUT(ctx, 1, "%s: got msg: %u bytes\n",
				 ctx->pc_ident, len + 1);

		if (ctx->pc_compare_buf != NULL) {
			int l = strlen(ctx->pc_compare_buf);
			C2_ASSERT(strlen(msg.pm_u.pm_str) == l + 5);
			C2_ASSERT(strncmp(ctx->pc_compare_buf,
					  msg.pm_u.pm_str, l) == 0);
			C2_ASSERT(strcmp(&msg.pm_u.pm_str[l], SEND_RESP) == 0);
			PING_OUT(ctx, 1, "%s: msg bytes validated\n",
				 ctx->pc_ident);
		}
		msg_free(&msg);
	}

	ping_buf_put(ctx, ev->nbe_buffer);

	C2_ALLOC_PTR(wi);
	c2_list_link_init(&wi->pwi_link);
	wi->pwi_type = C2_NET_QT_MSG_RECV;

	c2_mutex_lock(&ctx->pc_mutex);
	c2_list_add(&ctx->pc_work_queue, &wi->pwi_link);
	c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
	c2_mutex_unlock(&ctx->pc_mutex);
}

static void c_m_send_cb(const struct c2_net_buffer_event *ev)
{
	struct nlx_ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	struct ping_work_item *wi;

	C2_ASSERT(ev->nbe_buffer->nb_qtype == C2_NET_QT_MSG_SEND);
	PING_OUT(ctx, 1, "%s: Msg Send CB\n", ctx->pc_ident);

	if (ev->nbe_status < 0) {
		if (ev->nbe_status == -ECANCELED)
			PING_OUT(ctx, 1, "%s: msg send canceled\n",
				 ctx->pc_ident);
		else {
			ctx->pc_ops->pf("%s: msg send error: %d\n",
					ctx->pc_ident, ev->nbe_status);
			c2_atomic64_inc(&ctx->pc_errors);
		}

		/* let main app deal with it */
		C2_ALLOC_PTR(wi);
		c2_list_link_init(&wi->pwi_link);
		wi->pwi_type = C2_NET_QT_MSG_SEND;
		wi->pwi_nb = ev->nbe_buffer;

		c2_mutex_lock(&ctx->pc_mutex);
		c2_list_add(&ctx->pc_work_queue, &wi->pwi_link);
		c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
		c2_mutex_unlock(&ctx->pc_mutex);
	} else {
		c2_net_desc_free(&ev->nbe_buffer->nb_desc);
		ping_buf_put(ctx, ev->nbe_buffer);

		C2_ALLOC_PTR(wi);
		c2_list_link_init(&wi->pwi_link);
		wi->pwi_type = C2_NET_QT_MSG_SEND;

		c2_mutex_lock(&ctx->pc_mutex);
		c2_list_add(&ctx->pc_work_queue, &wi->pwi_link);
		c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
		c2_mutex_unlock(&ctx->pc_mutex);
	}
}

static void c_p_recv_cb(const struct c2_net_buffer_event *ev)
{
	struct nlx_ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	int rc;
	int len;
	struct ping_work_item *wi;
	struct ping_msg msg;

	C2_ASSERT(ev->nbe_buffer->nb_qtype == C2_NET_QT_PASSIVE_BULK_RECV);
	PING_OUT(ctx, 1, "%s: Passive Recv CB\n", ctx->pc_ident);

	if (ev->nbe_status < 0) {
		if (ev->nbe_status == -ECANCELED)
			PING_OUT(ctx, 1, "%s: passive recv canceled\n",
				 ctx->pc_ident);
		else {
			ctx->pc_ops->pf("%s: passive recv error: %d\n",
					ctx->pc_ident, ev->nbe_status);
			c2_atomic64_inc(&ctx->pc_errors);
		}
	} else {
		ev->nbe_buffer->nb_length = ev->nbe_length;
		C2_ASSERT(ev->nbe_offset == 0);
		rc = decode_msg(ev->nbe_buffer, ev->nbe_length, 0, &msg);
		C2_ASSERT(rc == 0);

		if (msg.pm_type != PM_MSG)
			C2_IMPOSSIBLE("Client: got desc\n");
		len = strlen(msg.pm_u.pm_str);
		if (strlen(msg.pm_u.pm_str) < 32)
			PING_OUT(ctx, 1, "%s: got data: %s\n",
				 ctx->pc_ident, msg.pm_u.pm_str);
		else
			PING_OUT(ctx, 1, "%s: got data: %u bytes\n",
				 ctx->pc_ident, len + 1);
		C2_ASSERT(ev->nbe_length == len + 2);
		if (strcmp(msg.pm_u.pm_str, DEF_RESPONSE) != 0) {
			int i;
			for (i = 0; i < len - 1; ++i) {
				if (msg.pm_u.pm_str[i] != "abcdefghi"[i % 9]) {
					PING_ERR("%s: data diff @ offset %i: "
						 "%c != %c\n",
						 ctx->pc_ident, i,
						 msg.pm_u.pm_str[i],
						 "abcdefghi"[i % 9]);
					c2_atomic64_inc(&ctx->pc_errors);
					break;
				}
			}
			if (i == len - 1)
				PING_OUT(ctx, 1, "%s: data bytes validated\n",
					 ctx->pc_ident);
		}
		msg_free(&msg);
	}

	c2_net_desc_free(&ev->nbe_buffer->nb_desc);
	ping_buf_put(ctx, ev->nbe_buffer);

	C2_ALLOC_PTR(wi);
	c2_list_link_init(&wi->pwi_link);
	wi->pwi_type = C2_NET_QT_PASSIVE_BULK_RECV;

	c2_mutex_lock(&ctx->pc_mutex);
	c2_list_add(&ctx->pc_work_queue, &wi->pwi_link);
	c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
	c2_mutex_unlock(&ctx->pc_mutex);
}

static void c_p_send_cb(const struct c2_net_buffer_event *ev)
{
	struct nlx_ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	struct ping_work_item *wi;

	C2_ASSERT(ev->nbe_buffer->nb_qtype == C2_NET_QT_PASSIVE_BULK_SEND);
	PING_OUT(ctx, 1, "%s: Passive Send CB\n", ctx->pc_ident);

	if (ev->nbe_status < 0) {
		if (ev->nbe_status == -ECANCELED)
			PING_OUT(ctx, 1, "%s: passive send canceled\n",
				 ctx->pc_ident);
		else {
			ctx->pc_ops->pf("%s: passive send error: %d\n",
					ctx->pc_ident, ev->nbe_status);
			c2_atomic64_inc(&ctx->pc_errors);
		}
	}

	c2_net_desc_free(&ev->nbe_buffer->nb_desc);
	ping_buf_put(ctx, ev->nbe_buffer);

	C2_ALLOC_PTR(wi);
	c2_list_link_init(&wi->pwi_link);
	wi->pwi_type = C2_NET_QT_PASSIVE_BULK_SEND;

	c2_mutex_lock(&ctx->pc_mutex);
	c2_list_add(&ctx->pc_work_queue, &wi->pwi_link);
	c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
	c2_mutex_unlock(&ctx->pc_mutex);
}

static void c_a_recv_cb(const struct c2_net_buffer_event *ev)
{
	C2_ASSERT(ev->nbe_buffer != NULL &&
		  ev->nbe_buffer->nb_qtype == C2_NET_QT_ACTIVE_BULK_RECV);
	C2_IMPOSSIBLE("Client: Active Recv CB\n");
}

static void c_a_send_cb(const struct c2_net_buffer_event *ev)
{
	C2_ASSERT(ev->nbe_buffer != NULL &&
		  ev->nbe_buffer->nb_qtype == C2_NET_QT_ACTIVE_BULK_SEND);
	C2_IMPOSSIBLE("Client: Active Send CB\n");
}

static void event_cb(const struct c2_net_tm_event *ev)
{
	struct nlx_ping_ctx *ctx = container_of(ev->nte_tm,
						struct nlx_ping_ctx,
						pc_tm);

	if (ev->nte_type == C2_NET_TEV_STATE_CHANGE) {
		const char *s = "unexpected";
		if (ev->nte_next_state == C2_NET_TM_STARTED)
			s = "started";
		else if (ev->nte_next_state == C2_NET_TM_STOPPED)
			s = "stopped";
		else if (ev->nte_next_state == C2_NET_TM_FAILED)
			s = "FAILED";
		PING_OUT(ctx, 1, "%s: Event CB state change to %s, status %d\n",
			 ctx->pc_ident, s, ev->nte_status);
		ctx->pc_status = ev->nte_status;
	} else if (ev->nte_type == C2_NET_TEV_ERROR) {
		PING_OUT(ctx, 0, "%s: Event CB for error %d\n",
			 ctx->pc_ident, ev->nte_status);
		c2_atomic64_inc(&ctx->pc_errors);
	} else if (ev->nte_type == C2_NET_TEV_DIAGNOSTIC)
		PING_OUT(ctx, 0, "%s: Event CB for diagnostic %d\n",
			 ctx->pc_ident, ev->nte_status);
}

static bool server_stop = false;

static struct c2_net_buffer_callbacks cbuf_cb = {
	.nbc_cb = {
		[C2_NET_QT_MSG_RECV]          = c_m_recv_cb,
		[C2_NET_QT_MSG_SEND]          = c_m_send_cb,
		[C2_NET_QT_PASSIVE_BULK_RECV] = c_p_recv_cb,
		[C2_NET_QT_PASSIVE_BULK_SEND] = c_p_send_cb,
		[C2_NET_QT_ACTIVE_BULK_RECV]  = c_a_recv_cb,
		[C2_NET_QT_ACTIVE_BULK_SEND]  = c_a_send_cb
	},
};

static struct c2_net_tm_callbacks ctm_cb = {
	.ntc_event_cb = event_cb
};

static void server_event_ident(char *buf, size_t len, const char *ident,
			       const struct c2_net_buffer_event *ev)
{
	const struct c2_net_end_point *ep = NULL;
	if (ev != NULL && ev->nbe_buffer != NULL) {
		if (ev->nbe_buffer->nb_qtype == C2_NET_QT_MSG_RECV) {
			if (ev->nbe_status == 0)
				ep = ev->nbe_ep;
		} else {
			ep = ev->nbe_buffer->nb_ep;
		}
	}
	if (ep != NULL)
		snprintf(buf, len, "%s (peer %s)", ident, ep->nep_addr);
	else
		snprintf(buf, len, "%s", ident);
}

static struct c2_atomic64 s_msg_recv_counter;

/* server callbacks */
static void s_m_recv_cb(const struct c2_net_buffer_event *ev)
{
	struct nlx_ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	int rc;
	struct ping_work_item *wi;
	struct ping_msg msg;
	int64_t count;
	char idbuf[IDBUF_LEN];
	int bulk_delay = ctx->pc_server_bulk_delay;


	C2_ASSERT(ev->nbe_buffer != NULL &&
		  ev->nbe_buffer->nb_qtype == C2_NET_QT_MSG_RECV);
	server_event_ident(idbuf, ARRAY_SIZE(idbuf), ctx->pc_ident, ev);
	count = c2_atomic64_add_return(&s_msg_recv_counter, 1);
	PING_OUT(ctx, 1, "%s: Msg Recv CB %ld 0x%lx\n", idbuf, (long int) count,
		 (unsigned long int) ev->nbe_buffer->nb_flags);
	if (ev->nbe_status < 0) {
		if (ev->nbe_status == -ECANCELED && server_stop)
			PING_OUT(ctx, 1, "%s: msg recv canceled on shutdown\n",
				 idbuf);
		else {
			ctx->pc_ops->pf("%s: msg recv error: %d\n",
					idbuf, ev->nbe_status);
			c2_atomic64_inc(&ctx->pc_errors);

			ev->nbe_buffer->nb_ep = NULL;
			C2_ASSERT(!(ev->nbe_buffer->nb_flags &
				    C2_NET_BUF_QUEUED));
			ev->nbe_buffer->nb_timeout = C2_TIME_NEVER;
			rc = c2_net_buffer_add(ev->nbe_buffer, &ctx->pc_tm);
			C2_ASSERT(rc == 0);
		}
	} else {
		struct c2_net_buffer *nb;

		rc = decode_msg(ev->nbe_buffer, ev->nbe_length, ev->nbe_offset,
				&msg);
		C2_ASSERT(rc == 0);

		nb = ping_buf_get(ctx);
		if (nb == NULL) {
			ctx->pc_ops->pf("%s: dropped msg, "
					"no buffer available\n", idbuf);
			c2_atomic64_inc(&ctx->pc_errors);
		} else {
			C2_ALLOC_PTR(wi);
			nb->nb_ep = ev->nbe_ep; /* save for later, if set */
			ev->nbe_buffer->nb_ep = NULL;
			c2_list_link_init(&wi->pwi_link);
			wi->pwi_nb = nb;
			if (msg.pm_type == PM_SEND_DESC) {
				PING_OUT(ctx, 1, "%s: got desc for "
					 "active recv\n", idbuf);
				wi->pwi_type = C2_NET_QT_ACTIVE_BULK_RECV;
				nb->nb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
				c2_net_desc_copy(&msg.pm_u.pm_desc,
						 &nb->nb_desc);
				nb->nb_ep = NULL; /* not needed */
				C2_ASSERT(rc == 0);
				if (bulk_delay != 0) {
					PING_OUT(ctx, 1, "%s: delay %d secs\n",
						 idbuf, bulk_delay);
					ping_sleep_secs(bulk_delay);
				}
			} else if (msg.pm_type == PM_RECV_DESC) {
				PING_OUT(ctx, 1, "%s: got desc for "
					 "active send\n", idbuf);
				wi->pwi_type = C2_NET_QT_ACTIVE_BULK_SEND;
				nb->nb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
				c2_net_desc_copy(&msg.pm_u.pm_desc,
						 &nb->nb_desc);
				nb->nb_ep = NULL; /* not needed */
				/* reuse encode_msg for convenience */
				if (ctx->pc_passive_size == 0)
					rc = encode_msg(nb, DEF_RESPONSE);
				else {
					char *bp;
					int i;
					bp = c2_alloc(ctx->pc_passive_size);
					C2_ASSERT(bp != NULL);
					for (i = 0;
					     i < ctx->pc_passive_size - 1; ++i)
						bp[i] = "abcdefghi"[i % 9];
					PING_OUT(ctx, 1, "%s: sending data "
						 "%d bytes\n", idbuf,
						 ctx->pc_passive_size);
					rc = encode_msg(nb, bp);
					c2_free(bp);
					C2_ASSERT(rc == 0);
				}
				C2_ASSERT(rc == 0);
				if (bulk_delay != 0) {
					PING_OUT(ctx, 1, "%s: delay %d secs\n",
						 idbuf, bulk_delay);
					ping_sleep_secs(bulk_delay);
				}
			} else {
				char *data;
				int len = strlen(msg.pm_u.pm_str);
				if (strlen(msg.pm_u.pm_str) < 32)
					PING_OUT(ctx, 1, "%s: got msg: %s\n",
						 idbuf, msg.pm_u.pm_str);
				else
					PING_OUT(ctx, 1, "%s: got msg: "
						 "%u bytes\n",
						 idbuf, len + 1);

				/* queue wi to send back ping response */
				data = c2_alloc(len + 6);
				c2_net_end_point_get(nb->nb_ep);
				wi->pwi_type = C2_NET_QT_MSG_SEND;
				nb->nb_qtype = C2_NET_QT_MSG_SEND;
				strcpy(data, msg.pm_u.pm_str);
				strcat(data, SEND_RESP);
				rc = encode_msg(nb, data);
				c2_free(data);
				C2_ASSERT(rc == 0);
			}
			c2_mutex_lock(&ctx->pc_mutex);
			c2_list_add(&ctx->pc_work_queue, &wi->pwi_link);
			if (ctx->pc_sync_events)
				c2_chan_signal(&ctx->pc_wq_chan);
			else
				c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
			c2_mutex_unlock(&ctx->pc_mutex);
		}
		ev->nbe_buffer->nb_ep = NULL;
		if (!(ev->nbe_buffer->nb_flags & C2_NET_BUF_QUEUED)) {
			ev->nbe_buffer->nb_timeout = C2_TIME_NEVER;
			PING_OUT(ctx, 1, "%s: re-queuing buffer\n",
				 ctx->pc_ident);
			rc = c2_net_buffer_add(ev->nbe_buffer, &ctx->pc_tm);
			C2_ASSERT(rc == 0);
		}

		msg_free(&msg);
	}
}

static void s_m_send_cb(const struct c2_net_buffer_event *ev)
{
	struct nlx_ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	int rc;
	char idbuf[IDBUF_LEN];

	C2_ASSERT(ev->nbe_buffer->nb_qtype == C2_NET_QT_MSG_SEND);
	server_event_ident(idbuf, ARRAY_SIZE(idbuf), ctx->pc_ident, ev);
	PING_OUT(ctx, 1, "%s: Msg Send CB\n", idbuf);

	if (ev->nbe_status < 0) {
		/* no retries here */
		if (ev->nbe_status == -ECANCELED)
			PING_OUT(ctx, 1, "%s: msg send canceled\n", idbuf);
		else {
			ctx->pc_ops->pf("%s: msg send error: %d\n",
					idbuf, ev->nbe_status);
			c2_atomic64_inc(&ctx->pc_errors);
		}
	}

	rc = c2_net_end_point_put(ev->nbe_buffer->nb_ep);
	C2_ASSERT(rc == 0);
	ev->nbe_buffer->nb_ep = NULL;

	ping_buf_put(ctx, ev->nbe_buffer);
}

static void s_p_recv_cb(const struct c2_net_buffer_event *ev)
{
	C2_ASSERT(ev->nbe_buffer != NULL &&
		  ev->nbe_buffer->nb_qtype == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_IMPOSSIBLE("Server: Passive Recv CB\n");
}

static void s_p_send_cb(const struct c2_net_buffer_event *ev)
{
	C2_ASSERT(ev->nbe_buffer != NULL &&
		  ev->nbe_buffer->nb_qtype == C2_NET_QT_PASSIVE_BULK_SEND);
	C2_IMPOSSIBLE("Server: Passive Send CB\n");
}

static void s_a_recv_cb(const struct c2_net_buffer_event *ev)
{
	struct nlx_ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	int rc;
	int len;
	struct ping_msg msg;
	char idbuf[IDBUF_LEN];

	C2_ASSERT(ev->nbe_buffer->nb_qtype == C2_NET_QT_ACTIVE_BULK_RECV);
	server_event_ident(idbuf, ARRAY_SIZE(idbuf), ctx->pc_ident, ev);
	PING_OUT(ctx, 1, "%s: Active Recv CB\n", idbuf);

	if (ev->nbe_status < 0) {
		/* no retries here */
		if (ev->nbe_status == -ECANCELED)
			PING_OUT(ctx, 1, "%s: active recv canceled\n", idbuf);
		else {
			ctx->pc_ops->pf("%s: active recv error: %d\n",
					idbuf, ev->nbe_status);
			c2_atomic64_inc(&ctx->pc_errors);
		}
	} else {
		ev->nbe_buffer->nb_length = ev->nbe_length;
		C2_ASSERT(ev->nbe_offset == 0);
		rc = decode_msg(ev->nbe_buffer, ev->nbe_length, 0, &msg);
		C2_ASSERT(rc == 0);

		if (msg.pm_type != PM_MSG)
			C2_IMPOSSIBLE("Server: got desc\n");
		len = strlen(msg.pm_u.pm_str);
		if (len < 32)
			PING_OUT(ctx, 1, "%s: got data: %s\n",
				 idbuf, msg.pm_u.pm_str);
		else
			PING_OUT(ctx, 1, "%s: got data: %u bytes\n",
				 idbuf, len + 1);
		C2_ASSERT(ev->nbe_length == len + 2);
		if (strcmp(msg.pm_u.pm_str, DEF_SEND) != 0) {
			int i;
			for (i = 0; i < len - 1; ++i) {
				if (msg.pm_u.pm_str[i] != "abcdefghi"[i % 9]) {
					PING_ERR("%s: data diff @ offset %i: "
						 "%c != %c\n", idbuf, i,
						 msg.pm_u.pm_str[i],
						 "abcdefghi"[i % 9]);
					c2_atomic64_inc(&ctx->pc_errors);
					break;
				}
			}
			if (i == len - 1)
				PING_OUT(ctx, 1, "%s: data bytes validated\n",
					 idbuf);
		}

		msg_free(&msg);
	}

	c2_net_desc_free(&ev->nbe_buffer->nb_desc);
	ping_buf_put(ctx, ev->nbe_buffer);
}

static void s_a_send_cb(const struct c2_net_buffer_event *ev)
{
	struct nlx_ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	char idbuf[IDBUF_LEN];

	C2_ASSERT(ev->nbe_buffer->nb_qtype == C2_NET_QT_ACTIVE_BULK_SEND);
	server_event_ident(idbuf, ARRAY_SIZE(idbuf), ctx->pc_ident, ev);
	PING_OUT(ctx, 1, "%s: Active Send CB\n", idbuf);

	if (ev->nbe_status < 0) {
		/* no retries here */
		if (ev->nbe_status == -ECANCELED)
			PING_OUT(ctx, 1, "%s: active send canceled\n", idbuf);
		else {
			ctx->pc_ops->pf("%s: active send error: %d\n",
					idbuf, ev->nbe_status);
			c2_atomic64_inc(&ctx->pc_errors);
		}
	}

	c2_net_desc_free(&ev->nbe_buffer->nb_desc);
	ping_buf_put(ctx, ev->nbe_buffer);
}

static struct c2_net_buffer_callbacks sbuf_cb = {
	.nbc_cb = {
		[C2_NET_QT_MSG_RECV]          = s_m_recv_cb,
		[C2_NET_QT_MSG_SEND]          = s_m_send_cb,
		[C2_NET_QT_PASSIVE_BULK_RECV] = s_p_recv_cb,
		[C2_NET_QT_PASSIVE_BULK_SEND] = s_p_send_cb,
		[C2_NET_QT_ACTIVE_BULK_RECV]  = s_a_recv_cb,
		[C2_NET_QT_ACTIVE_BULK_SEND]  = s_a_send_cb
	},
};

static struct c2_net_tm_callbacks stm_cb = {
	.ntc_event_cb = event_cb
};

static void ping_fini(struct nlx_ping_ctx *ctx);

static bool ping_workq_clink_cb(struct c2_clink *cl)
{
	struct nlx_ping_ctx *ctx =
		container_of(cl, struct nlx_ping_ctx, pc_wq_clink);
	++ctx->pc_wq_signal_count;
	return false;
}

static bool ping_net_clink_cb(struct c2_clink *cl)
{
	struct nlx_ping_ctx *ctx =
		container_of(cl, struct nlx_ping_ctx, pc_net_clink);
	++ctx->pc_net_signal_count;
	return false;
}

/**
   Initialise a ping client or server.
   Calls all the required c2_net APIs in the correct order, with
   cleanup on failure.
   On success, the transfer machine is started.
   @param ctx the client/server context.  pc_xprt, pc_nr_bufs, pc_tm,
   pc_hostname, pc_port and pc_id must be initialised by the caller.
   @retval 0 success
   @retval -errno failure
 */
static int ping_init(struct nlx_ping_ctx *ctx)
{
	int                i;
	int                rc;
	char               addr[C2_NET_LNET_XEP_ADDR_LEN];
	struct c2_clink    tmwait;

	c2_list_init(&ctx->pc_work_queue);
	c2_atomic64_set(&ctx->pc_errors, 0);
	c2_atomic64_set(&ctx->pc_retries, 0);
	ctx->pc_interfaces = NULL;
	if (ctx->pc_sync_events) {
		c2_chan_init(&ctx->pc_wq_chan);
		c2_chan_init(&ctx->pc_net_chan);

		c2_clink_init(&ctx->pc_wq_clink, &ping_workq_clink_cb);
		c2_clink_attach(&ctx->pc_net_clink, &ctx->pc_wq_clink,
				&ping_net_clink_cb); /* group */

		c2_clink_add(&ctx->pc_wq_chan, &ctx->pc_wq_clink);
		c2_clink_add(&ctx->pc_net_chan, &ctx->pc_net_clink);
	}

	rc = c2_net_domain_init(&ctx->pc_dom, ctx->pc_xprt);
	if (rc != 0) {
		PING_ERR("domain init failed: %d\n", rc);
		goto fail;
	}

	if (ctx->pc_dom_debug > 0)
		c2_net_lnet_dom_set_debug(&ctx->pc_dom, ctx->pc_dom_debug);

	rc = c2_net_lnet_ifaces_get(&ctx->pc_dom, &ctx->pc_interfaces);
	if (rc != 0) {
		PING_ERR("failed to load interface names: %d\n", rc);
		goto fail;
	}
	C2_ASSERT(ctx->pc_interfaces != NULL);

	if (ctx->pc_interfaces[0] == NULL) {
		PING_ERR("no interfaces defined locally\n");
		goto fail;
	}

	rc = alloc_buffers(ctx->pc_nr_bufs, ctx->pc_segments, ctx->pc_seg_size,
			   ctx->pc_seg_shift, &ctx->pc_nbs);
	if (rc != 0) {
		PING_ERR("buffer allocation failed: %d\n", rc);
		goto fail;
	}
	rc = c2_bitmap_init(&ctx->pc_nbbm, ctx->pc_nr_bufs);
	if (rc != 0) {
		PING_ERR("buffer bitmap allocation failed: %d\n", rc);
		goto fail;
	}
	C2_ASSERT(ctx->pc_buf_callbacks != NULL);
	for (i = 0; i < ctx->pc_nr_bufs; ++i) {
		rc = c2_net_buffer_register(&ctx->pc_nbs[i], &ctx->pc_dom);
		if (rc != 0) {
			PING_ERR("buffer register failed: %d\n", rc);
			goto fail;
		}
		ctx->pc_nbs[i].nb_callbacks = ctx->pc_buf_callbacks;
	}

	if (ctx->pc_network == NULL) {
		ctx->pc_network = ctx->pc_interfaces[0];
		for (i = 0; ctx->pc_interfaces[i] != NULL; ++i) {
			if (strstr(ctx->pc_interfaces[i], "@lo") != NULL)
				continue;
			ctx->pc_network = ctx->pc_interfaces[i]; /* 1st !@lo */
			break;
		}
	}
	if (ctx->pc_rnetwork == NULL)
		ctx->pc_rnetwork = ctx->pc_network;

	if (ctx->pc_tmid >= 0)
		snprintf(addr, ARRAY_SIZE(addr), "%s:%u:%u:%u", ctx->pc_network,
			 ctx->pc_pid, ctx->pc_portal, ctx->pc_tmid);
	else
		snprintf(addr, ARRAY_SIZE(addr), "%s:%u:%u:*", ctx->pc_network,
			 ctx->pc_pid, ctx->pc_portal);

	rc = c2_net_tm_init(&ctx->pc_tm, &ctx->pc_dom);
	if (rc != 0) {
		PING_ERR("transfer machine init failed: %d\n", rc);
		goto fail;
	}

	if (ctx->pc_tm_debug > 0)
		c2_net_lnet_tm_set_debug(&ctx->pc_tm, ctx->pc_tm_debug);

	if (ctx->pc_sync_events) {
		rc = c2_net_buffer_event_deliver_synchronously(&ctx->pc_tm);
		C2_ASSERT(rc == 0);
	}

	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&ctx->pc_tm.ntm_chan, &tmwait);
	rc = c2_net_tm_start(&ctx->pc_tm, addr);
	if (rc != 0) {
		PING_ERR("transfer machine start failed: %d\n", rc);
		goto fail;
	}

	/* wait for tm to notify it has started */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	if (ctx->pc_tm.ntm_state != C2_NET_TM_STARTED) {
		rc = ctx->pc_status;
		if (rc == 0)
			rc = -EINVAL;
		PING_ERR("transfer machine start failed: %d\n", rc);
		goto fail;
	}

	return rc;
fail:
	ping_fini(ctx);
	return rc;
}

static inline bool ping_tm_timedwait(struct nlx_ping_ctx *ctx,
				     struct c2_clink *cl,
				     c2_time_t timeout)
{
	bool signalled = false;
	if (timeout == C2_TIME_NEVER) {
		if (ctx->pc_sync_events) {
			do {
				timeout = c2_time_from_now(0, 50 * ONE_MILLION);
				signalled = c2_chan_timedwait(cl, timeout);
				c2_net_buffer_event_deliver_all(&ctx->pc_tm);
			} while (!signalled);
		} else
			c2_chan_wait(cl);
	} else {
		signalled = c2_chan_timedwait(cl, timeout);
		if (ctx->pc_sync_events)
			c2_net_buffer_event_deliver_all(&ctx->pc_tm);
	}
	return signalled;
}

static inline void ping_tm_wait(struct nlx_ping_ctx *ctx,
				struct c2_clink *cl)
{
	ping_tm_timedwait(ctx, cl, C2_TIME_NEVER);
}

static void ping_fini(struct nlx_ping_ctx *ctx)
{
	struct c2_list_link *link;
	struct ping_work_item *wi;

	if (ctx->pc_tm.ntm_state != C2_NET_TM_UNDEFINED) {
		if (ctx->pc_tm.ntm_state != C2_NET_TM_FAILED) {
			struct c2_clink tmwait;
			c2_clink_init(&tmwait, NULL);
			c2_clink_add(&ctx->pc_tm.ntm_chan, &tmwait);
			c2_net_tm_stop(&ctx->pc_tm, true);
			while (ctx->pc_tm.ntm_state != C2_NET_TM_STOPPED) {
				/* wait for it to stop */
				c2_time_t timeout = c2_time_from_now(0,
							     50 * ONE_MILLION);
				c2_chan_timedwait(&tmwait, timeout);
			}
			c2_clink_del(&tmwait);
		}

		if (ctx->pc_ops->pqs != NULL)
			(*ctx->pc_ops->pqs)(ctx, false);

		c2_net_tm_fini(&ctx->pc_tm);
	}
	if (ctx->pc_nbs != NULL) {
		int i;
		for (i = 0; i < ctx->pc_nr_bufs; ++i) {
			struct c2_net_buffer *nb = &ctx->pc_nbs[i];
			C2_ASSERT(nb->nb_flags == C2_NET_BUF_REGISTERED);
			c2_net_buffer_deregister(nb, &ctx->pc_dom);
			c2_bufvec_free_aligned(&nb->nb_buffer,
					       ctx->pc_seg_shift);
		}
		c2_free(ctx->pc_nbs);
		c2_bitmap_fini(&ctx->pc_nbbm);
	}
	if (ctx->pc_interfaces != NULL)
		c2_net_lnet_ifaces_put(&ctx->pc_dom, &ctx->pc_interfaces);
	if (ctx->pc_dom.nd_xprt != NULL)
		c2_net_domain_fini(&ctx->pc_dom);

	while (!c2_list_is_empty(&ctx->pc_work_queue)) {
		link = c2_list_first(&ctx->pc_work_queue);
		wi = c2_list_entry(link, struct ping_work_item, pwi_link);
		c2_list_del(&wi->pwi_link);
		c2_free(wi);
	}
	if (ctx->pc_sync_events) {
		c2_clink_del(&ctx->pc_net_clink);
		c2_clink_del(&ctx->pc_wq_clink);

		c2_clink_fini(&ctx->pc_net_clink);
		c2_clink_fini(&ctx->pc_wq_clink);

		c2_chan_fini(&ctx->pc_net_chan);
		c2_chan_fini(&ctx->pc_wq_chan);
	}

	c2_list_fini(&ctx->pc_work_queue);
}

static void set_msg_timeout(struct nlx_ping_ctx *ctx,
			    struct c2_net_buffer *nb)
{
	if (ctx->pc_msg_timeout > 0) {
		PING_OUT(ctx, 1, "%s: setting msg nb_timeout to %ds\n",
			 ctx->pc_ident, ctx->pc_msg_timeout);
		nb->nb_timeout =
			ping_c2_time_after_secs(ctx->pc_msg_timeout);
	} else {
		nb->nb_timeout = C2_TIME_NEVER;
	}
}

static void set_bulk_timeout(struct nlx_ping_ctx *ctx,
			     struct c2_net_buffer *nb)
{
	if (ctx->pc_bulk_timeout > 0) {
		PING_OUT(ctx, 1, "%s: setting bulk nb_timeout to %ds\n",
			 ctx->pc_ident, ctx->pc_bulk_timeout);
		nb->nb_timeout =
			ping_c2_time_after_secs(ctx->pc_bulk_timeout);
	} else {
		nb->nb_timeout = C2_TIME_NEVER;
	}
}

static void nlx_ping_server_work(struct nlx_ping_ctx *ctx)
{
	struct c2_list_link *link;
	struct ping_work_item *wi;
	int rc;

	C2_ASSERT(c2_mutex_is_locked(&ctx->pc_mutex));

	while (!c2_list_is_empty(&ctx->pc_work_queue)) {
		link = c2_list_first(&ctx->pc_work_queue);
		wi = c2_list_entry(link, struct ping_work_item,
				   pwi_link);
		switch (wi->pwi_type) {
		case C2_NET_QT_MSG_SEND:
			set_msg_timeout(ctx, wi->pwi_nb);
			rc = c2_net_buffer_add(wi->pwi_nb, &ctx->pc_tm);
			break;
		case C2_NET_QT_ACTIVE_BULK_SEND:
		case C2_NET_QT_ACTIVE_BULK_RECV:
			set_bulk_timeout(ctx, wi->pwi_nb);
			rc = c2_net_buffer_add(wi->pwi_nb, &ctx->pc_tm);
			break;
		default:
			C2_IMPOSSIBLE("unexpected wi->pwi_type");
		}
		if (rc != 0) {
			c2_atomic64_inc(&ctx->pc_errors);
			ctx->pc_ops->pf("%s buffer_add(%d) failed %d\n",
					ctx->pc_ident, wi->pwi_type, rc);
		}
		c2_list_del(&wi->pwi_link);
		c2_free(wi);
	}
}

static void nlx_ping_server_async(struct nlx_ping_ctx *ctx)
{
	c2_time_t timeout;

	C2_ASSERT(c2_mutex_is_locked(&ctx->pc_mutex));

	while (!server_stop) {
		nlx_ping_server_work(ctx);
		timeout = c2_time_from_now(5, 0);
		c2_cond_timedwait(&ctx->pc_cond, &ctx->pc_mutex, timeout);
	}

	return;
}

static void nlx_ping_server_sync(struct nlx_ping_ctx *ctx)
{
	c2_time_t timeout;
	bool signalled;

	C2_ASSERT(c2_mutex_is_locked(&ctx->pc_mutex));

	while (!server_stop) {
		while (!server_stop &&
		       c2_list_is_empty(&ctx->pc_work_queue) &&
		       !c2_net_buffer_event_pending(&ctx->pc_tm)) {
			++ctx->pc_blocked_count;
			c2_net_buffer_event_notify(&ctx->pc_tm,
						   &ctx->pc_net_chan);
			c2_mutex_unlock(&ctx->pc_mutex);
			/* wait on the channel group */
			timeout = c2_time_from_now(15, 0);
			signalled = c2_chan_timedwait(&ctx->pc_wq_clink,
						      timeout);
			c2_mutex_lock(&ctx->pc_mutex);
		}

		++ctx->pc_worked_count;

		if (c2_net_buffer_event_pending(&ctx->pc_tm)) {
			c2_mutex_unlock(&ctx->pc_mutex);
			/* deliver events synchronously on this thread */
			c2_net_buffer_event_deliver_all(&ctx->pc_tm);
			c2_mutex_lock(&ctx->pc_mutex);
		}

		if (server_stop) {
			PING_OUT(ctx, 1, "%s stopping\n", ctx->pc_ident);
			break;
		}

		nlx_ping_server_work(ctx);
	}

	return;
}


static void nlx_ping_server(struct nlx_ping_ctx *ctx)
{
	int i;
	int rc;
	struct c2_net_buffer *nb;
	struct c2_clink tmwait;
	int num_recv_bufs = max32u(ctx->pc_nr_bufs / 4, 2);
	int buf_size;

	ctx->pc_tm.ntm_callbacks = &stm_cb;
	ctx->pc_buf_callbacks = &sbuf_cb;

	ctx->pc_ident = "Server";
	C2_ASSERT(ctx->pc_nr_bufs > 2);
	C2_ASSERT(num_recv_bufs >= 2);
	rc = ping_init(ctx);
	C2_ASSERT(rc == 0);
	C2_ASSERT(ctx->pc_network != NULL);
	ping_print_interfaces(ctx);
	ctx->pc_ops->pf("Server end point: %s\n", ctx->pc_tm.ntm_ep->nep_addr);

	buf_size = ctx->pc_segments * ctx->pc_seg_size;
	if (ctx->pc_max_recv_msgs > 0 && ctx->pc_min_recv_size <= 0)
		ctx->pc_min_recv_size = buf_size / ctx->pc_max_recv_msgs;
	else if (ctx->pc_min_recv_size > 0 && ctx->pc_max_recv_msgs <= 0)
		ctx->pc_max_recv_msgs = buf_size / ctx->pc_min_recv_size;

	if (ctx->pc_min_recv_size < PING_DEF_MIN_RECV_SIZE ||
	    ctx->pc_min_recv_size > buf_size ||
	    ctx->pc_max_recv_msgs < 1)
		ctx->pc_max_recv_msgs = ctx->pc_min_recv_size = -1;

	if (ctx->pc_min_recv_size <= 0 && ctx->pc_max_recv_msgs <= 0) {
		ctx->pc_min_recv_size = PING_DEF_MIN_RECV_SIZE;
		ctx->pc_max_recv_msgs = buf_size / ctx->pc_min_recv_size;
	}
	C2_ASSERT(ctx->pc_min_recv_size >= PING_DEF_MIN_RECV_SIZE);
	C2_ASSERT(ctx->pc_max_recv_msgs >= 1);
	ctx->pc_ops->pf("%s receive buffer parameters:\n"
			"\tnum_recv_bufs=%d\n"
			"\tmin_recv_size=%d\n\tmax_recv_msgs=%d",
			ctx->pc_ident, num_recv_bufs,
			ctx->pc_min_recv_size, ctx->pc_max_recv_msgs);

	c2_mutex_lock(&ctx->pc_mutex);
	for (i = 0; i < num_recv_bufs; ++i) {
		nb = &ctx->pc_nbs[i];
		nb->nb_qtype = C2_NET_QT_MSG_RECV;
		nb->nb_timeout = C2_TIME_NEVER;
		nb->nb_ep = NULL;
		nb->nb_min_receive_size = ctx->pc_min_recv_size;
		nb->nb_max_receive_msgs = ctx->pc_max_recv_msgs;
		rc = c2_net_buffer_add(nb, &ctx->pc_tm);
		c2_bitmap_set(&ctx->pc_nbbm, i, true);
		C2_ASSERT(rc == 0);
	}

	/* startup synchronization handshake */
	ctx->pc_ready = true;
	c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
	while (ctx->pc_ready)
		c2_cond_wait(&ctx->pc_cond, &ctx->pc_mutex);

	if (ctx->pc_sync_events)
		nlx_ping_server_sync(ctx);
	else
		nlx_ping_server_async(ctx);
	c2_mutex_unlock(&ctx->pc_mutex);

	/* dequeue recv buffers */
	c2_clink_init(&tmwait, NULL);

	for (i = 0; i < num_recv_bufs; ++i) {
		nb = &ctx->pc_nbs[i];
		c2_clink_add(&ctx->pc_tm.ntm_chan, &tmwait);
		c2_net_buffer_del(nb, &ctx->pc_tm);
		c2_bitmap_set(&ctx->pc_nbbm, i, false);
		ping_tm_wait(ctx, &tmwait);
		c2_clink_del(&tmwait);
	}

	/* wait for active buffers to flush */
	c2_clink_add(&ctx->pc_tm.ntm_chan, &tmwait);
	for (i = 0; i < C2_NET_QT_NR; ++i)
		while (!c2_net_tm_tlist_is_empty(&ctx->pc_tm.ntm_q[i])) {
			PING_OUT(ctx, 1, "waiting for queue %d to empty\n", i);
			ping_tm_wait(ctx, &tmwait);
		}
	c2_clink_del(&tmwait);
	c2_clink_fini(&tmwait);

	ping_fini(ctx);
	server_stop = false;
}

void nlx_ping_server_should_stop(struct nlx_ping_ctx *ctx)
{
	c2_mutex_lock(&ctx->pc_mutex);
	server_stop = true;
	if (ctx->pc_sync_events)
		c2_chan_signal(&ctx->pc_wq_chan);
	else
		c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
	c2_mutex_unlock(&ctx->pc_mutex);
}

void nlx_ping_server_spawn(struct c2_thread *server_thread,
			   struct nlx_ping_ctx *sctx)
{
	int rc;

	sctx->pc_xprt = &c2_net_lnet_xprt;
	sctx->pc_segments = PING_SERVER_SEGMENTS;
	sctx->pc_seg_size = PING_SERVER_SEGMENT_SIZE;
	sctx->pc_seg_shift = PING_SERVER_SEGMENT_SHIFT;
	sctx->pc_pid = C2_NET_LNET_PID;

	c2_mutex_lock(&sctx->pc_mutex);
	C2_SET0(server_thread);
	rc = C2_THREAD_INIT(server_thread, struct nlx_ping_ctx *,
			    NULL, &nlx_ping_server, sctx, "ping_server");
	C2_ASSERT(rc == 0);
	while (!sctx->pc_ready)
		c2_cond_wait(&sctx->pc_cond, &sctx->pc_mutex);
	sctx->pc_ready = false;
	c2_cond_signal(&sctx->pc_cond, &sctx->pc_mutex);
	c2_mutex_unlock(&sctx->pc_mutex);
}

/**
   Test an RPC-like exchange, sending data in a message to the server and
   getting back a response.
   @param ctx client context
   @param server_ep endpoint of the server
   @param data data to send, or NULL to send a default "ping"
   @retval 0 successful test
   @retval -errno failed to send to server
 */
static int nlx_ping_client_msg_send_recv(struct nlx_ping_ctx *ctx,
					 struct c2_net_end_point *server_ep,
					 const char *data)
{
	int rc;
	struct c2_net_buffer *nb;
	struct c2_list_link *link;
	struct ping_work_item *wi;
	int recv_done = 0;
	int retries = SEND_RETRIES;
	c2_time_t session_timeout = C2_TIME_NEVER;

	if (data == NULL)
		data = "ping";
	ctx->pc_compare_buf = data;

	PING_OUT(ctx, 1, "%s: starting msg send/recv sequence\n",
		 ctx->pc_ident);
	/* queue buffer for response, must do before sending msg */
	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	nb->nb_qtype = C2_NET_QT_MSG_RECV;
	nb->nb_timeout = C2_TIME_NEVER;
	nb->nb_ep = NULL;
	nb->nb_min_receive_size = ctx->pc_segments * ctx->pc_seg_size;
	nb->nb_max_receive_msgs = 1;
	rc = c2_net_buffer_add(nb, &ctx->pc_tm);
	C2_ASSERT(rc == 0);

	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	rc = encode_msg(nb, data);
	nb->nb_qtype = C2_NET_QT_MSG_SEND;
	nb->nb_ep = server_ep;
	C2_ASSERT(rc == 0);
	set_msg_timeout(ctx, nb);
	rc = c2_net_buffer_add(nb, &ctx->pc_tm);
	C2_ASSERT(rc == 0);

	/* wait for receive response to complete */
	c2_mutex_lock(&ctx->pc_mutex);
	while (1) {
		while (!c2_list_is_empty(&ctx->pc_work_queue)) {
			link = c2_list_first(&ctx->pc_work_queue);
			wi = c2_list_entry(link, struct ping_work_item,
					   pwi_link);
			c2_list_del(&wi->pwi_link);
			if (wi->pwi_type == C2_NET_QT_MSG_RECV) {
				ctx->pc_compare_buf = NULL;
				recv_done++;
			} else if (wi->pwi_type == C2_NET_QT_MSG_SEND &&
				   wi->pwi_nb != NULL) {
				c2_time_t delay;
				/* send error, retry a few times */
				if (retries == 0) {
					ctx->pc_compare_buf = NULL;
					ctx->pc_ops->pf("%s: send failed, "
							"no more retries\n",
							ctx->pc_ident);
					c2_mutex_unlock(&ctx->pc_mutex);
					ping_buf_put(ctx, nb);
					c2_free(wi);
					c2_atomic64_inc(&ctx->pc_errors);
					return -ETIMEDOUT;
				}
				c2_time_set(&delay,
					    SEND_RETRIES + 1 - retries, 0);
				--retries;
				c2_nanosleep(delay, NULL);
				c2_atomic64_inc(&ctx->pc_retries);
				set_msg_timeout(ctx, nb);
				rc = c2_net_buffer_add(nb, &ctx->pc_tm);
				C2_ASSERT(rc == 0);
			} else if (wi->pwi_type == C2_NET_QT_MSG_SEND) {
				recv_done++;
				if (ctx->pc_msg_timeout > 0)
					session_timeout =
						ping_c2_time_after_secs(
							   ctx->pc_msg_timeout);
			}
			c2_free(wi);
		}
		if (recv_done == 2)
			break;
		if (session_timeout == C2_TIME_NEVER)
			c2_cond_wait(&ctx->pc_cond, &ctx->pc_mutex);
		else if (!c2_cond_timedwait(&ctx->pc_cond, &ctx->pc_mutex,
					    session_timeout)) {
			ctx->pc_ops->pf("%s: Receive TIMED OUT\n",
					ctx->pc_ident);
			rc = -ETIMEDOUT;
			break;
		}
	}

	c2_mutex_unlock(&ctx->pc_mutex);
	return rc;
}

static int nlx_ping_client_passive_recv(struct nlx_ping_ctx *ctx,
					struct c2_net_end_point *server_ep)
{
	int rc;
	struct c2_net_buffer *nb;
	struct c2_net_buf_desc nbd;
	struct c2_list_link *link;
	struct ping_work_item *wi;
	int recv_done = 0;
	int retries = SEND_RETRIES;

	PING_OUT(ctx, 1, "%s: starting passive recv sequence\n", ctx->pc_ident);
	/* queue our passive receive buffer */
	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	nb->nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	nb->nb_ep = server_ep;
	set_bulk_timeout(ctx, nb);
	rc = c2_net_buffer_add(nb, &ctx->pc_tm);
	C2_ASSERT(rc == 0);
	rc = c2_net_desc_copy(&nb->nb_desc, &nbd);
	C2_ASSERT(rc == 0);

	/* send descriptor in message to server */
	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	rc = encode_desc(nb, false, &nbd);
	c2_net_desc_free(&nbd);
	nb->nb_qtype = C2_NET_QT_MSG_SEND;
	nb->nb_ep = server_ep;
	C2_ASSERT(rc == 0);
	nb->nb_timeout = C2_TIME_NEVER;
	rc = c2_net_buffer_add(nb, &ctx->pc_tm);
	C2_ASSERT(rc == 0);

	/* wait for receive to complete */
	c2_mutex_lock(&ctx->pc_mutex);
	while (1) {
		while (!c2_list_is_empty(&ctx->pc_work_queue)) {
			link = c2_list_first(&ctx->pc_work_queue);
			wi = c2_list_entry(link, struct ping_work_item,
					   pwi_link);
			c2_list_del(&wi->pwi_link);
			if (wi->pwi_type == C2_NET_QT_PASSIVE_BULK_RECV)
				recv_done++;
			else if (wi->pwi_type == C2_NET_QT_MSG_SEND &&
				 wi->pwi_nb != NULL) {
				c2_time_t delay;
				/* send error, retry a few times */
				if (retries == 0) {
					ctx->pc_ops->pf("%s: send failed, "
							"no more retries\n",
							ctx->pc_ident);
					c2_net_desc_free(&nb->nb_desc);
					c2_mutex_unlock(&ctx->pc_mutex);
					ping_buf_put(ctx, nb);
					c2_atomic64_inc(&ctx->pc_errors);
					return -ETIMEDOUT;
				}
				c2_time_set(&delay,
					    SEND_RETRIES + 1 - retries, 0);
				--retries;
				c2_nanosleep(delay, NULL);
				c2_atomic64_inc(&ctx->pc_retries);
				set_msg_timeout(ctx, nb);
				rc = c2_net_buffer_add(nb, &ctx->pc_tm);
				C2_ASSERT(rc == 0);
			} else if (wi->pwi_type == C2_NET_QT_MSG_SEND) {
				recv_done++;
			}
			c2_free(wi);
		}
		if (recv_done == 2)
			break;
		c2_cond_wait(&ctx->pc_cond, &ctx->pc_mutex);
	}

	c2_mutex_unlock(&ctx->pc_mutex);
	return rc;
}

static int nlx_ping_client_passive_send(struct nlx_ping_ctx *ctx,
					struct c2_net_end_point *server_ep,
					const char *data)
{
	int rc;
	struct c2_net_buffer *nb;
	struct c2_net_buf_desc nbd;
	struct c2_list_link *link;
	struct ping_work_item *wi;
	int send_done = 0;
	int retries = SEND_RETRIES;

	if (data == NULL)
		data = "passive ping";
	PING_OUT(ctx, 1, "%s: starting passive send sequence\n", ctx->pc_ident);
	/* queue our passive receive buffer */
	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	/* reuse encode_msg for convenience */
	rc = encode_msg(nb, data);
	nb->nb_qtype = C2_NET_QT_PASSIVE_BULK_SEND;
	nb->nb_ep = server_ep;
	set_bulk_timeout(ctx, nb);
	rc = c2_net_buffer_add(nb, &ctx->pc_tm);
	C2_ASSERT(rc == 0);
	rc = c2_net_desc_copy(&nb->nb_desc, &nbd);
	C2_ASSERT(rc == 0);

	/* send descriptor in message to server */
	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	rc = encode_desc(nb, true, &nbd);
	c2_net_desc_free(&nbd);
	nb->nb_qtype = C2_NET_QT_MSG_SEND;
	nb->nb_ep = server_ep;
	C2_ASSERT(rc == 0);
	nb->nb_timeout = C2_TIME_NEVER;
	rc = c2_net_buffer_add(nb, &ctx->pc_tm);
	C2_ASSERT(rc == 0);

	/* wait for send to complete */
	c2_mutex_lock(&ctx->pc_mutex);
	while (1) {
		while (!c2_list_is_empty(&ctx->pc_work_queue)) {
			link = c2_list_first(&ctx->pc_work_queue);
			wi = c2_list_entry(link, struct ping_work_item,
					   pwi_link);
			c2_list_del(&wi->pwi_link);
			if (wi->pwi_type == C2_NET_QT_PASSIVE_BULK_SEND)
				send_done++;
			else if (wi->pwi_type == C2_NET_QT_MSG_SEND &&
				 wi->pwi_nb != NULL) {
				c2_time_t delay;
				/* send error, retry a few times */
				if (retries == 0) {
					ctx->pc_ops->pf("%s: send failed, "
							"no more retries\n",
							ctx->pc_ident);
					c2_net_desc_free(&nb->nb_desc);
					c2_mutex_unlock(&ctx->pc_mutex);
					ping_buf_put(ctx, nb);
					c2_atomic64_inc(&ctx->pc_errors);
					return -ETIMEDOUT;
				}
				c2_time_set(&delay,
					    SEND_RETRIES + 1 - retries, 0);
				--retries;
				c2_nanosleep(delay, NULL);
				c2_atomic64_inc(&ctx->pc_retries);
				set_msg_timeout(ctx, nb);
				rc = c2_net_buffer_add(nb, &ctx->pc_tm);
				C2_ASSERT(rc == 0);
			} else if (wi->pwi_type == C2_NET_QT_MSG_SEND) {
				send_done++;
			}
			c2_free(wi);
		}
		if (send_done == 2)
			break;
		c2_cond_wait(&ctx->pc_cond, &ctx->pc_mutex);
	}

	c2_mutex_unlock(&ctx->pc_mutex);
	return rc;
}

static int nlx_ping_client_init(struct nlx_ping_ctx *ctx,
			 struct c2_net_end_point **server_ep)
{
	int rc;
	char addr[C2_NET_LNET_XEP_ADDR_LEN];
	const char *fmt = "Client %s";
	char *ident;

	C2_ALLOC_ARR(ident, ARRAY_SIZE(addr) + strlen(fmt) + 1);
	if (ident == NULL)
		return -ENOMEM;
	sprintf(ident, fmt, "starting"); /* temporary */
	ctx->pc_ident = ident;

	ctx->pc_tm.ntm_callbacks = &ctm_cb;
	ctx->pc_buf_callbacks = &cbuf_cb;
	rc = ping_init(ctx);
	if (rc != 0) {
		c2_free(ident);
		ctx->pc_ident = NULL;
		return rc;
	}
	C2_ASSERT(ctx->pc_network != NULL);
	C2_ASSERT(ctx->pc_rnetwork != NULL);

	/* need end point for the server */
	snprintf(addr, ARRAY_SIZE(addr), "%s:%u:%u:%u", ctx->pc_rnetwork,
		 ctx->pc_rpid, ctx->pc_rportal, ctx->pc_rtmid);
	rc = c2_net_end_point_create(server_ep, &ctx->pc_tm, addr);
	if (rc != 0) {
		ping_fini(ctx);
		c2_free(ident);
		ctx->pc_ident = NULL;
		return rc;
	}

	/* clients can have dynamically assigned TMIDs so use the EP addr
	   in the ident.
	 */
	sprintf(ident, fmt, ctx->pc_tm.ntm_ep->nep_addr);
	return 0;
}

static int nlx_ping_client_fini(struct nlx_ping_ctx *ctx,
			 struct c2_net_end_point *server_ep)
{
	int rc = c2_net_end_point_put(server_ep);
	ping_fini(ctx);
	if (ctx->pc_ident != NULL) {
		c2_free((void *)ctx->pc_ident);
		ctx->pc_ident = NULL;
	}
	return rc;
}

void nlx_ping_client(struct nlx_ping_client_params *params)
{
	int			 i;
	int			 rc;
	struct c2_net_end_point *server_ep;
	char			*bp = NULL;
	struct nlx_ping_ctx	 cctx = {
		.pc_xprt = &c2_net_lnet_xprt,
		.pc_ops  = params->ops,
		.pc_nr_bufs = params->nr_bufs,
		.pc_segments = PING_CLIENT_SEGMENTS,
		.pc_seg_size = PING_CLIENT_SEGMENT_SIZE,
		.pc_seg_shift = PING_CLIENT_SEGMENT_SHIFT,
		.pc_passive_size = params->passive_size,
		.pc_tm = {
			.ntm_state     = C2_NET_TM_UNDEFINED
		},
		.pc_bulk_timeout = params->bulk_timeout,
		.pc_msg_timeout = params->msg_timeout,

		.pc_network = params->client_network,
		.pc_pid     = params->client_pid,
		.pc_portal  = params->client_portal,
		.pc_tmid    = params->client_tmid,

		.pc_rnetwork = params->server_network,
		.pc_rpid     = params->server_pid,
		.pc_rportal  = params->server_portal,
		.pc_rtmid    = params->server_tmid,
		.pc_dom_debug = params->debug,
		.pc_tm_debug  = params->debug,
		.pc_verbose  = params->verbose,

		.pc_sync_events = false,
	};

	c2_mutex_init(&cctx.pc_mutex);
	c2_cond_init(&cctx.pc_cond);
	rc = nlx_ping_client_init(&cctx, &server_ep);
	if (rc != 0)
		goto fail;

	if (params->client_id == 1)
		ping_print_interfaces(&cctx);

	if (params->passive_size != 0) {
		bp = c2_alloc(params->passive_size);
		C2_ASSERT(bp != NULL);
		for (i = 0; i < params->passive_size - 1; ++i)
			bp[i] = "abcdefghi"[i % 9];
	}

	for (i = 1; i <= params->loops; ++i) {
		PING_OUT(&cctx, 1, "%s: Loop %d\n", cctx.pc_ident, i);
		rc = nlx_ping_client_msg_send_recv(&cctx, server_ep, bp);
		if (rc != 0)
			break;
		rc = nlx_ping_client_passive_recv(&cctx, server_ep);
		if (rc != 0)
			break;
		rc = nlx_ping_client_passive_send(&cctx, server_ep, bp);
		if (rc != 0)
			break;
	}

	cctx.pc_ops->pqs(&cctx, false);
	rc = nlx_ping_client_fini(&cctx, server_ep);
	c2_free(bp);
	C2_ASSERT(rc == 0);
fail:
	c2_cond_fini(&cctx.pc_cond);
	c2_mutex_fini(&cctx.pc_mutex);
}

void nlx_ping_init()
{
	c2_mutex_init(&qstats_mutex);
}

void nlx_ping_fini()
{
	c2_mutex_fini(&qstats_mutex);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
