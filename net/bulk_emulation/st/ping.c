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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 04/12/2011
 */

#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h" /* strlen */
#include "net/net.h"
#include "net/bulk_mem.h"
#include "net/bulk_emulation/st/ping.h"

#define DEF_RESPONSE "active pong"
#define DEF_SEND "passive ping"
#define SEND_RESP    " pong"
/** Descriptor for the tlist of buffers. */

enum {
	SEND_RETRIES = 3,
};

struct ping_work_item {
	enum c2_net_queue_type      pwi_type;
	struct c2_net_buffer       *pwi_nb;
	struct c2_list_link         pwi_link;
};

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

int alloc_buffers(int num, uint32_t segs, c2_bcount_t segsize,
		  struct c2_net_buffer **out)
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
		rc = c2_bufvec_alloc(&nb->nb_buffer, segs, segsize);
		if (rc != 0)
			break;
	}

	if (rc == 0)
		*out = nbs;
	else {
		while (--i >= 0)
			c2_bufvec_free(&nbs[i].nb_buffer);
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
struct c2_net_buffer *ping_buf_get(struct ping_ctx *ctx)
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
void ping_buf_put(struct ping_ctx *ctx, struct c2_net_buffer *nb)
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
int encode_msg(struct c2_net_buffer *nb, const char *str)
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
int encode_desc(struct c2_net_buffer *nb,
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
int decode_msg(struct c2_net_buffer *nb, struct ping_msg *msg)
{
	struct c2_bufvec_cursor cur;
	char *bp;
	c2_bcount_t step;

	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	bp = c2_bufvec_cursor_addr(&cur);
	C2_ASSERT(*bp == 'm' || *bp == 's' || *bp == 'r');
	C2_ASSERT(!c2_bufvec_cursor_move(&cur, 1));
	if (*bp == 'm') {
		c2_bcount_t len = nb->nb_length - 1;
		void *str;
		struct c2_bufvec out = C2_BUFVEC_INIT_BUF(&str, &len);
		struct c2_bufvec_cursor outcur;

		msg->pm_type = PM_MSG;
		str = msg->pm_u.pm_str = c2_alloc(len + 1);
		c2_bufvec_cursor_init(&outcur, &out);
		step = c2_bufvec_cursor_copy(&outcur, &cur, len);
		C2_ASSERT(step == len);
	} else {
		int len;
		msg->pm_type = (*bp == 's') ? PM_SEND_DESC : PM_RECV_DESC;
		bp = c2_bufvec_cursor_addr(&cur);
		step = c2_bufvec_cursor_step(&cur);
		C2_ASSERT(step >= 9 && bp[8] == 0);
		C2_ASSERT(sscanf(bp, "%d", &len) == 1);
		msg->pm_u.pm_desc.nbd_len = len;
		C2_ASSERT(step >= 9 + msg->pm_u.pm_desc.nbd_len);
		bp += 9;
		msg->pm_u.pm_desc.nbd_data = c2_alloc(len);
		memcpy(msg->pm_u.pm_desc.nbd_data, bp, len);
	}
	return 0;
}

void msg_free(struct ping_msg *msg)
{
	if (msg->pm_type != PM_MSG)
		c2_net_desc_free(&msg->pm_u.pm_desc);
	else
		c2_free(msg->pm_u.pm_str);
}

static struct ping_ctx *
buffer_event_to_ping_ctx(const struct c2_net_buffer_event *ev)
{
	struct c2_net_buffer *nb = ev->nbe_buffer;
	C2_ASSERT(nb != NULL);
	return container_of(nb->nb_tm, struct ping_ctx, pc_tm);
}

/* client callbacks */
void c_m_recv_cb(const struct c2_net_buffer_event *ev)
{
	struct ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	int rc;
	int len;
	struct ping_work_item *wi;
	struct ping_msg msg;

	C2_ASSERT(ev->nbe_buffer->nb_qtype == C2_NET_QT_MSG_RECV);
	ctx->pc_ops->pf("%s: Msg Recv CB\n", ctx->pc_ident);

	if (ev->nbe_status < 0) {
		if (ev->nbe_status == -ECANCELED)
			ctx->pc_ops->pf("%s: msg recv canceled\n",
					ctx->pc_ident);
		else
			ctx->pc_ops->pf("%s: msg recv error: %d\n",
					ctx->pc_ident, ev->nbe_status);
	} else {
		ev->nbe_buffer->nb_length = ev->nbe_length;
		rc = decode_msg(ev->nbe_buffer, &msg);
		C2_ASSERT(rc == 0);

		if (msg.pm_type != PM_MSG)
			C2_IMPOSSIBLE("Client: got desc\n");

		len = strlen(msg.pm_u.pm_str);
		if (strlen(msg.pm_u.pm_str) < 32)
			ctx->pc_ops->pf("%s: got msg: %s\n",
					ctx->pc_ident, msg.pm_u.pm_str);
		else
			ctx->pc_ops->pf("%s: got msg: %u bytes\n",
					ctx->pc_ident, len + 1);

		if (ctx->pc_compare_buf != NULL) {
			int l = strlen(ctx->pc_compare_buf);
			C2_ASSERT(strlen(msg.pm_u.pm_str) == l + 5);
			C2_ASSERT(strncmp(ctx->pc_compare_buf,
					  msg.pm_u.pm_str, l) == 0);
			C2_ASSERT(strcmp(&msg.pm_u.pm_str[l], SEND_RESP) == 0);
			ctx->pc_ops->pf("%s: msg bytes validated\n",
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

void c_m_send_cb(const struct c2_net_buffer_event *ev)
{
	struct ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	struct ping_work_item *wi;

	C2_ASSERT(ev->nbe_buffer->nb_qtype == C2_NET_QT_MSG_SEND);
	ctx->pc_ops->pf("%s: Msg Send CB\n", ctx->pc_ident);

	if (ev->nbe_status < 0) {
		if (ev->nbe_status == -ECANCELED)
			ctx->pc_ops->pf("%s: msg send canceled\n",
					ctx->pc_ident);
		else
			ctx->pc_ops->pf("%s: msg send error: %d\n",
					ctx->pc_ident, ev->nbe_status);

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

void c_p_recv_cb(const struct c2_net_buffer_event *ev)
{
	struct ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	int rc;
	int len;
	struct ping_work_item *wi;
	struct ping_msg msg;

	C2_ASSERT(ev->nbe_buffer->nb_qtype == C2_NET_QT_PASSIVE_BULK_RECV);
	ctx->pc_ops->pf("%s: Passive Recv CB\n", ctx->pc_ident);

	if (ev->nbe_status < 0) {
		if (ev->nbe_status == -ECANCELED)
			ctx->pc_ops->pf("%s: passive recv canceled\n",
					ctx->pc_ident);
		else
			ctx->pc_ops->pf("%s: passive recv error: %d\n",
					ctx->pc_ident, ev->nbe_status);
	} else {
		ev->nbe_buffer->nb_length = ev->nbe_length;
		rc = decode_msg(ev->nbe_buffer, &msg);
		C2_ASSERT(rc == 0);

		if (msg.pm_type != PM_MSG)
			C2_IMPOSSIBLE("Client: got desc\n");
		len = strlen(msg.pm_u.pm_str);
		if (strlen(msg.pm_u.pm_str) < 32)
			ctx->pc_ops->pf("%s: got data: %s\n",
					ctx->pc_ident, msg.pm_u.pm_str);
		else
			ctx->pc_ops->pf("%s: got data: %u bytes\n",
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
					break;
				}
			}
			if (i == len - 1)
				ctx->pc_ops->pf("%s: data bytes validated\n",
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

void c_p_send_cb(const struct c2_net_buffer_event *ev)
{
	struct ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	struct ping_work_item *wi;

	C2_ASSERT(ev->nbe_buffer->nb_qtype == C2_NET_QT_PASSIVE_BULK_SEND);
	ctx->pc_ops->pf("%s: Passive Send CB\n", ctx->pc_ident);

	if (ev->nbe_status < 0) {
		if (ev->nbe_status == -ECANCELED)
			ctx->pc_ops->pf("%s: passive send canceled\n",
					ctx->pc_ident);
		else
			ctx->pc_ops->pf("%s: passive send error: %d\n",
					ctx->pc_ident, ev->nbe_status);
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

void c_a_recv_cb(const struct c2_net_buffer_event *ev)
{
	C2_ASSERT(ev->nbe_buffer != NULL &&
		  ev->nbe_buffer->nb_qtype == C2_NET_QT_ACTIVE_BULK_RECV);
	C2_IMPOSSIBLE("Client: Active Recv CB\n");
}

void c_a_send_cb(const struct c2_net_buffer_event *ev)
{
	C2_ASSERT(ev->nbe_buffer != NULL &&
		  ev->nbe_buffer->nb_qtype == C2_NET_QT_ACTIVE_BULK_SEND);
	C2_IMPOSSIBLE("Client: Active Send CB\n");
}

void event_cb(const struct c2_net_tm_event *ev)
{
	struct ping_ctx *ctx = container_of(ev->nte_tm, struct ping_ctx, pc_tm);

	if (ev->nte_type == C2_NET_TEV_STATE_CHANGE) {
		const char *s = "unexpected";
		if (ev->nte_next_state == C2_NET_TM_STARTED)
			s = "started";
		else if (ev->nte_next_state == C2_NET_TM_STOPPED)
			s = "stopped";
		else if (ev->nte_next_state == C2_NET_TM_FAILED)
			s = "FAILED";
		ctx->pc_ops->pf("%s: Event CB state change to %s, status %d\n",
				ctx->pc_ident, s, ev->nte_status);
		ctx->pc_status = ev->nte_status;
	} else if (ev->nte_type == C2_NET_TEV_ERROR)
		ctx->pc_ops->pf("%s: Event CB for error %d\n",
				ctx->pc_ident, ev->nte_status);
	else if (ev->nte_type == C2_NET_TEV_DIAGNOSTIC)
		ctx->pc_ops->pf("%s: Event CB for diagnostic %d\n",
				ctx->pc_ident, ev->nte_status);
}

bool server_stop = false;

struct c2_net_buffer_callbacks cbuf_cb = {
	.nbc_cb = {
		[C2_NET_QT_MSG_RECV]          = c_m_recv_cb,
		[C2_NET_QT_MSG_SEND]          = c_m_send_cb,
		[C2_NET_QT_PASSIVE_BULK_RECV] = c_p_recv_cb,
		[C2_NET_QT_PASSIVE_BULK_SEND] = c_p_send_cb,
		[C2_NET_QT_ACTIVE_BULK_RECV]  = c_a_recv_cb,
		[C2_NET_QT_ACTIVE_BULK_SEND]  = c_a_send_cb
	},
};

struct c2_net_tm_callbacks ctm_cb = {
	.ntc_event_cb = event_cb
};

static void server_event_ident(char *buf, const char *ident,
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
		sprintf(buf, "%s (peer %s)", ident, ep->nep_addr);
	else
		strcpy(buf, ident);
}

static struct c2_atomic64 s_msg_recv_counter;

/* server callbacks */
void s_m_recv_cb(const struct c2_net_buffer_event *ev)
{
	struct ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	int rc;
	struct ping_work_item *wi;
	struct ping_msg msg;
	int64_t count;
	char idbuf[64];
	int bulk_delay = ctx->pc_server_bulk_delay;


	C2_ASSERT(ev->nbe_buffer != NULL &&
		  ev->nbe_buffer->nb_qtype == C2_NET_QT_MSG_RECV);
	server_event_ident(idbuf, ctx->pc_ident, ev);
	count = c2_atomic64_add_return(&s_msg_recv_counter, 1);
	ctx->pc_ops->pf("%s: Msg Recv CB %" PRId64 "\n", idbuf,
			(long long int) count);
	if (ev->nbe_status < 0) {
		if (ev->nbe_status == -ECANCELED && server_stop)
			ctx->pc_ops->pf("%s: msg recv canceled on shutdown\n",
					idbuf);
		else {
			ctx->pc_ops->pf("%s: msg recv error: %d\n",
					idbuf, ev->nbe_status);
			ev->nbe_buffer->nb_timeout = C2_TIME_NEVER;
			ev->nbe_buffer->nb_ep = NULL;
			rc = c2_net_buffer_add(ev->nbe_buffer, &ctx->pc_tm);
			C2_ASSERT(rc == 0);
		}
	} else {
		struct c2_net_buffer *nb;

		ev->nbe_buffer->nb_length = ev->nbe_length;
		rc = decode_msg(ev->nbe_buffer, &msg);
		C2_ASSERT(rc == 0);

		nb = ping_buf_get(ctx);
		if (nb == NULL) {
			ctx->pc_ops->pf("%s: dropped msg, "
					"no buffer available\n", idbuf);
		} else {
			C2_ALLOC_PTR(wi);
			nb->nb_ep = ev->nbe_ep; /* save for later, if set */
			ev->nbe_buffer->nb_ep = NULL;
			c2_list_link_init(&wi->pwi_link);
			wi->pwi_nb = nb;
			if (msg.pm_type == PM_SEND_DESC) {
				ctx->pc_ops->pf("%s: got desc for "
						"active recv\n", idbuf);
				wi->pwi_type = C2_NET_QT_ACTIVE_BULK_RECV;
				nb->nb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
				c2_net_desc_copy(&msg.pm_u.pm_desc,
						 &nb->nb_desc);
				nb->nb_ep = NULL; /* not needed */
				C2_ASSERT(rc == 0);
				if (bulk_delay != 0) {
					ctx->pc_ops->pf("%s: delay %d secs\n",
							idbuf, bulk_delay);
					ping_sleep_secs(bulk_delay);
				}
			} else if (msg.pm_type == PM_RECV_DESC) {
				ctx->pc_ops->pf("%s: got desc for "
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
					ctx->pc_ops->pf("%s: sending data "
							"%d bytes\n", idbuf,
							ctx->pc_passive_size);
					rc = encode_msg(nb, bp);
					c2_free(bp);
					C2_ASSERT(rc == 0);
				}
				C2_ASSERT(rc == 0);
				if (bulk_delay != 0) {
					ctx->pc_ops->pf("%s: delay %d secs\n",
							idbuf, bulk_delay);
					ping_sleep_secs(bulk_delay);
				}
			} else {
				char *data;
				int len = strlen(msg.pm_u.pm_str);
				if (strlen(msg.pm_u.pm_str) < 32)
					ctx->pc_ops->pf("%s: got msg: %s\n",
							idbuf, msg.pm_u.pm_str);
				else
					ctx->pc_ops->pf("%s: got msg: "
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
			c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
			c2_mutex_unlock(&ctx->pc_mutex);
		}
		ev->nbe_buffer->nb_timeout = C2_TIME_NEVER;
		ev->nbe_buffer->nb_ep = NULL;
		rc = c2_net_buffer_add(ev->nbe_buffer, &ctx->pc_tm);
		C2_ASSERT(rc == 0);

		msg_free(&msg);
	}
}

void s_m_send_cb(const struct c2_net_buffer_event *ev)
{
	struct ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	int rc;
	char idbuf[64];

	C2_ASSERT(ev->nbe_buffer->nb_qtype == C2_NET_QT_MSG_SEND);
	server_event_ident(idbuf, ctx->pc_ident, ev);
	ctx->pc_ops->pf("%s: Msg Send CB\n", idbuf);

	if (ev->nbe_status < 0) {
		/* no retries here */
		if (ev->nbe_status == -ECANCELED)
			ctx->pc_ops->pf("%s: msg send canceled\n", idbuf);
		else
			ctx->pc_ops->pf("%s: msg send error: %d\n",
					idbuf, ev->nbe_status);
	}

	rc = c2_net_end_point_put(ev->nbe_buffer->nb_ep);
	C2_ASSERT(rc == 0);
	ev->nbe_buffer->nb_ep = NULL;

	ping_buf_put(ctx, ev->nbe_buffer);
}

void s_p_recv_cb(const struct c2_net_buffer_event *ev)
{
	C2_ASSERT(ev->nbe_buffer != NULL &&
		  ev->nbe_buffer->nb_qtype == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_IMPOSSIBLE("Server: Passive Recv CB\n");
}

void s_p_send_cb(const struct c2_net_buffer_event *ev)
{
	C2_ASSERT(ev->nbe_buffer != NULL &&
		  ev->nbe_buffer->nb_qtype == C2_NET_QT_PASSIVE_BULK_SEND);
	C2_IMPOSSIBLE("Server: Passive Send CB\n");
}

void s_a_recv_cb(const struct c2_net_buffer_event *ev)
{
	struct ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	int rc;
	int len;
	struct ping_msg msg;
	char idbuf[64];

	C2_ASSERT(ev->nbe_buffer->nb_qtype == C2_NET_QT_ACTIVE_BULK_RECV);
	server_event_ident(idbuf, ctx->pc_ident, ev);
	ctx->pc_ops->pf("%s: Active Recv CB\n", idbuf);

	if (ev->nbe_status < 0) {
		/* no retries here */
		if (ev->nbe_status == -ECANCELED)
			ctx->pc_ops->pf("%s: active recv canceled\n", idbuf);
		else
			ctx->pc_ops->pf("%s: active recv error: %d\n",
					idbuf, ev->nbe_status);
	} else {
		ev->nbe_buffer->nb_length = ev->nbe_length;
		rc = decode_msg(ev->nbe_buffer, &msg);
		C2_ASSERT(rc == 0);

		if (msg.pm_type != PM_MSG)
			C2_IMPOSSIBLE("Server: got desc\n");
		len = strlen(msg.pm_u.pm_str);
		if (len < 32)
			ctx->pc_ops->pf("%s: got data: %s\n",
					idbuf, msg.pm_u.pm_str);
		else
			ctx->pc_ops->pf("%s: got data: %u bytes\n",
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
					break;
				}
			}
			if (i == len - 1)
				ctx->pc_ops->pf("%s: data bytes validated\n",
						idbuf);
		}

		msg_free(&msg);
	}

	c2_net_desc_free(&ev->nbe_buffer->nb_desc);
	ping_buf_put(ctx, ev->nbe_buffer);
}

void s_a_send_cb(const struct c2_net_buffer_event *ev)
{
	struct ping_ctx *ctx = buffer_event_to_ping_ctx(ev);
	char idbuf[64];

	C2_ASSERT(ev->nbe_buffer->nb_qtype == C2_NET_QT_ACTIVE_BULK_SEND);
	server_event_ident(idbuf, ctx->pc_ident, ev);
	ctx->pc_ops->pf("%s: Active Send CB\n", idbuf);

	if (ev->nbe_status < 0) {
		/* no retries here */
		if (ev->nbe_status == -ECANCELED)
			ctx->pc_ops->pf("%s: active send canceled\n", idbuf);
		else
			ctx->pc_ops->pf("%s: active send error: %d\n",
					idbuf, ev->nbe_status);
	}

	c2_net_desc_free(&ev->nbe_buffer->nb_desc);
	ping_buf_put(ctx, ev->nbe_buffer);
}

struct c2_net_buffer_callbacks sbuf_cb = {
	.nbc_cb = {
		[C2_NET_QT_MSG_RECV]          = s_m_recv_cb,
		[C2_NET_QT_MSG_SEND]          = s_m_send_cb,
		[C2_NET_QT_PASSIVE_BULK_RECV] = s_p_recv_cb,
		[C2_NET_QT_PASSIVE_BULK_SEND] = s_p_send_cb,
		[C2_NET_QT_ACTIVE_BULK_RECV]  = s_a_recv_cb,
		[C2_NET_QT_ACTIVE_BULK_SEND]  = s_a_send_cb
	},
};

struct c2_net_tm_callbacks stm_cb = {
	.ntc_event_cb = event_cb
};

void ping_fini(struct ping_ctx *ctx);

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
int ping_init(struct ping_ctx *ctx)
{
	int                i;
	int                rc;
	char               addr[C2_NET_BULK_MEM_XEP_ADDR_LEN];
	struct c2_clink    tmwait;

	c2_list_init(&ctx->pc_work_queue);

	rc = c2_net_domain_init(&ctx->pc_dom, ctx->pc_xprt);
	if (rc != 0) {
		PING_ERR("domain init failed: %d\n", rc);
		goto fail;
	}

	rc = alloc_buffers(ctx->pc_nr_bufs, ctx->pc_segments, ctx->pc_seg_size,
			   &ctx->pc_nbs);
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

	if (ctx->pc_id != 0)
		sprintf(addr, "%s:%u:%u", ctx->pc_hostname, ctx->pc_port,
			ctx->pc_id);
	else
		sprintf(addr, "%s:%u", ctx->pc_hostname, ctx->pc_port);

	rc = c2_net_tm_init(&ctx->pc_tm, &ctx->pc_dom);
	if (rc != 0) {
		PING_ERR("transfer machine init failed: %d\n", rc);
		goto fail;
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

void ping_fini(struct ping_ctx *ctx)
{
	struct c2_list_link *link;
	struct ping_work_item *wi;

	if (ctx->pc_tm.ntm_state != C2_NET_TM_UNDEFINED) {
		if (ctx->pc_tm.ntm_state != C2_NET_TM_FAILED) {
			struct c2_clink tmwait;
			c2_clink_init(&tmwait, NULL);
			c2_clink_add(&ctx->pc_tm.ntm_chan, &tmwait);
			c2_net_tm_stop(&ctx->pc_tm, true);
			while (ctx->pc_tm.ntm_state != C2_NET_TM_STOPPED)
				c2_chan_wait(&tmwait); /* wait for it to stop */
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
			c2_bufvec_free(&nb->nb_buffer);
		}
		c2_free(ctx->pc_nbs);
		c2_bitmap_fini(&ctx->pc_nbbm);
	}
	if (ctx->pc_dom.nd_xprt != NULL)
		c2_net_domain_fini(&ctx->pc_dom);

	while (!c2_list_is_empty(&ctx->pc_work_queue)) {
		link = c2_list_first(&ctx->pc_work_queue);
		wi = c2_list_entry(link, struct ping_work_item, pwi_link);
		c2_list_del(&wi->pwi_link);
		c2_free(wi);
	}
	c2_list_fini(&ctx->pc_work_queue);
}

void ping_server(struct ping_ctx *ctx)
{
	int i;
	int rc;
	struct c2_net_buffer *nb;
	struct c2_clink tmwait;

	ctx->pc_tm.ntm_callbacks = &stm_cb;
	ctx->pc_buf_callbacks = &sbuf_cb;
	if (ctx->pc_hostname == NULL)
		ctx->pc_hostname = "127.0.0.1";
	if (ctx->pc_port == 0)
		ctx->pc_port = PING_PORT1;
	ctx->pc_ident = "Server";
	C2_ASSERT(ctx->pc_nr_bufs >= 20);
	rc = ping_init(ctx);
	C2_ASSERT(rc == 0);

	c2_mutex_lock(&ctx->pc_mutex);
	for (i = 0; i < (ctx->pc_nr_bufs / 4); ++i) {
		nb = &ctx->pc_nbs[i];
		nb->nb_qtype = C2_NET_QT_MSG_RECV;
		nb->nb_timeout = C2_TIME_NEVER;
		nb->nb_ep = NULL;
		nb->nb_min_receive_size = ctx->pc_segments * ctx->pc_seg_size;
		nb->nb_max_receive_msgs = 1;
		rc = c2_net_buffer_add(nb, &ctx->pc_tm);
		c2_bitmap_set(&ctx->pc_nbbm, i, true);
		C2_ASSERT(rc == 0);
	}

	while (!server_stop) {
		struct c2_list_link *link;
		struct ping_work_item *wi;
		while (!c2_list_is_empty(&ctx->pc_work_queue)) {
			link = c2_list_first(&ctx->pc_work_queue);
			wi = c2_list_entry(link, struct ping_work_item,
					   pwi_link);
			switch (wi->pwi_type) {
			case C2_NET_QT_MSG_SEND:
			case C2_NET_QT_ACTIVE_BULK_SEND:
			case C2_NET_QT_ACTIVE_BULK_RECV:
				wi->pwi_nb->nb_timeout = C2_TIME_NEVER;
				rc = c2_net_buffer_add(wi->pwi_nb, &ctx->pc_tm);
				C2_ASSERT(rc == 0);
				break;
			default:
				C2_IMPOSSIBLE("unexpected wi->pwi_type");
			}
			c2_list_del(&wi->pwi_link);
			c2_free(wi);
		}
		c2_cond_wait(&ctx->pc_cond, &ctx->pc_mutex);
	}
	c2_mutex_unlock(&ctx->pc_mutex);

	/* dequeue recv buffers */
	c2_clink_init(&tmwait, NULL);

	for (i = 0; i < (ctx->pc_nr_bufs / 4); ++i) {
		nb = &ctx->pc_nbs[i];
		c2_clink_add(&ctx->pc_tm.ntm_chan, &tmwait);
		c2_net_buffer_del(nb, &ctx->pc_tm);
		c2_bitmap_set(&ctx->pc_nbbm, i, false);
		C2_ASSERT(rc == 0);
		c2_chan_wait(&tmwait);
		c2_clink_del(&tmwait);
	}

	/* wait for active buffers to flush */
	c2_clink_add(&ctx->pc_tm.ntm_chan, &tmwait);
	for (i = 0; i < C2_NET_QT_NR; ++i)
		while (!c2_net_tm_tlist_is_empty(&ctx->pc_tm.ntm_q[i])) {
			ctx->pc_ops->pf("waiting for queue %d to empty\n", i);
			c2_chan_wait(&tmwait);
		}
	c2_clink_del(&tmwait);
	c2_clink_fini(&tmwait);

	ping_fini(ctx);
	server_stop = false;
}

void ping_server_should_stop(struct ping_ctx *ctx)
{
	c2_mutex_lock(&ctx->pc_mutex);
	server_stop = true;
	c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
	c2_mutex_unlock(&ctx->pc_mutex);
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
int ping_client_msg_send_recv(struct ping_ctx *ctx,
			      struct c2_net_end_point *server_ep,
			      const char *data)
{
	int rc;
	struct c2_net_buffer *nb;
	struct c2_list_link *link;
	struct ping_work_item *wi;
	int recv_done = 0;
	int retries = SEND_RETRIES;

	if (data == NULL)
		data = "ping";
	ctx->pc_compare_buf = data;

	ctx->pc_ops->pf("%s: starting msg send/recv sequence\n", ctx->pc_ident);
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
	nb->nb_timeout = C2_TIME_NEVER;
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
					return -ETIMEDOUT;
				}
				c2_time_set(&delay,
					    SEND_RETRIES + 1 - retries, 0);
				--retries;
				c2_nanosleep(delay, NULL);
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

int ping_client_passive_recv(struct ping_ctx *ctx,
			     struct c2_net_end_point *server_ep)
{
	int rc;
	struct c2_net_buffer *nb;
	struct c2_net_buf_desc nbd;
	struct c2_list_link *link;
	struct ping_work_item *wi;
	int recv_done = 0;
	int retries = SEND_RETRIES;

	ctx->pc_ops->pf("%s: starting passive recv sequence\n", ctx->pc_ident);
	/* queue our passive receive buffer */
	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	nb->nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	nb->nb_ep = server_ep;
	if (ctx->pc_passive_bulk_timeout > 0) {
		ctx->pc_ops->pf("%s: setting nb_timeout to %ds\n",
				ctx->pc_ident, ctx->pc_passive_bulk_timeout);
		nb->nb_timeout =
			ping_c2_time_after_secs(ctx->pc_passive_bulk_timeout);
	} else {
		nb->nb_timeout = C2_TIME_NEVER;
	}
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
					return -ETIMEDOUT;
				}
				c2_time_set(&delay,
					    SEND_RETRIES + 1 - retries, 0);
				--retries;
				c2_nanosleep(delay, NULL);
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

int ping_client_passive_send(struct ping_ctx *ctx,
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
	ctx->pc_ops->pf("%s: starting passive send sequence\n", ctx->pc_ident);
	/* queue our passive receive buffer */
	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	/* reuse encode_msg for convenience */
	rc = encode_msg(nb, data);
	nb->nb_qtype = C2_NET_QT_PASSIVE_BULK_SEND;
	nb->nb_ep = server_ep;
	if (ctx->pc_passive_bulk_timeout > 0) {
		ctx->pc_ops->pf("%s: setting nb_timeout to %ds\n",
				ctx->pc_ident, ctx->pc_passive_bulk_timeout);
		nb->nb_timeout =
			ping_c2_time_after_secs(ctx->pc_passive_bulk_timeout);
	} else {
		nb->nb_timeout = C2_TIME_NEVER;
	}
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
					return -ETIMEDOUT;
				}
				c2_time_set(&delay,
					    SEND_RETRIES + 1 - retries, 0);
				--retries;
				c2_nanosleep(delay, NULL);
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

int ping_client_init(struct ping_ctx *ctx, struct c2_net_end_point **server_ep)
{
	int rc;
	char addr[C2_NET_BULK_MEM_XEP_ADDR_LEN];

	ctx->pc_tm.ntm_callbacks = &ctm_cb;
	ctx->pc_buf_callbacks = &cbuf_cb;
	if (ctx->pc_hostname == NULL)
		ctx->pc_hostname = "127.0.0.1";
	if (ctx->pc_rhostname == NULL)
		ctx->pc_rhostname = "127.0.0.1";
	if (ctx->pc_port == 0)
		ctx->pc_port = PING_PORT2;
	if (ctx->pc_rport == 0)
		ctx->pc_rport = PING_PORT1;
	if (ctx->pc_ident == NULL)
		ctx->pc_ident = "Client";
	rc = ping_init(ctx);
	if (rc != 0)
		return rc;

	/* need end point for the server */
	if (ctx->pc_rid != 0)
		sprintf(addr, "%s:%u:%u", ctx->pc_rhostname, ctx->pc_rport,
			ctx->pc_rid);
	else
		sprintf(addr, "%s:%u", ctx->pc_rhostname, ctx->pc_rport);
	rc = c2_net_end_point_create(server_ep, &ctx->pc_tm, addr);
	return rc;
}

int ping_client_fini(struct ping_ctx *ctx, struct c2_net_end_point *server_ep)
{
	int rc = c2_net_end_point_put(server_ep);
	ping_fini(ctx);
	return rc;
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
