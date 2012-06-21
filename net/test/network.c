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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 03/22/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef __KERNEL__
#include <linux/kernel.h>	/* snprintf() */
#else
#include <stdio.h>		/* snprintf() */
#include <inttypes.h>		/* PRIu64 */
#endif

#include "lib/cdefs.h"		/* ergo */
#include "lib/errno.h"		/* E2BIG */
#include "lib/memory.h"		/* C2_ALLOC_ARR */
#include "lib/misc.h"		/* C2_SET0 */
#include "lib/vec.h"		/* C2_SEG_SHIFT */

#include "net/net.h"
#include "net/lnet/lnet.h"	/* c2_net_lnet_xprt */

#include "net/test/network.h"

/**
   @defgroup NetTestNetworkInternals Colibri Network Benchmark Network Internals

   @todo add timeouts to channels and network buffers
   @todo align code (function parameters etc.)
   @todo cache c2_vec_count()

   @see
   @ref net-test

   @{
 */

#if 0
#ifndef __KERNEL__
#define DEBUG_NET_TEST_NETWORK
#endif
#endif

#ifdef __KERNEL__
/* see buf_desc_decode() and c2_net_test_network_bd_encode() */
C2_BASSERT(sizeof(unsigned)	 == sizeof(uint32_t));
C2_BASSERT(sizeof(unsigned long) == sizeof(c2_bcount_t));
#endif

enum {
	C2_NET_TEST_STRLEN_NBD_HEADER = 32,
};

int c2_net_test_network_init(void)
{
	return c2_net_xprt_init(&c2_net_lnet_xprt);
}

void c2_net_test_network_fini(void)
{
	c2_net_xprt_fini(&c2_net_lnet_xprt);
}

/**
   Get net-test network context for the buffer event.
 */
static struct c2_net_test_network_ctx *
cb_ctx_extract(const struct c2_net_buffer_event *ev)
{
	return ev->nbe_buffer->nb_app_private;
}

/**
   Get buffer number in net-test network context for the buffer event.
 */
static uint32_t cb_buf_index_extract(const struct c2_net_buffer_event *ev,
				     struct c2_net_test_network_ctx *ctx,
				     enum c2_net_queue_type q)
{
	struct c2_net_buffer *arr;
	bool		      type_ping;
	int		      index;
	int		      index_max;

	C2_PRE(ctx != NULL);

	type_ping = q == C2_NET_QT_MSG_SEND || q == C2_NET_QT_MSG_RECV;
	arr = type_ping ? ctx->ntc_buf_ping : ctx->ntc_buf_bulk;
	index = ev->nbe_buffer - arr;
	index_max = type_ping ? ctx->ntc_buf_ping_nr : ctx->ntc_buf_bulk_nr;

	C2_POST(index >= 0 && index < index_max);

	return index;
}

/**
   Default callback for all network buffers.
   Calls user-defined callback for the buffer.
   @see net_test_buf_init()
 */
static void cb_default(const struct c2_net_buffer_event *ev,
		       enum c2_net_queue_type q)
{
	struct c2_net_buffer *buf = ev->nbe_buffer;
	struct c2_net_test_network_ctx *ctx;
	uint32_t buf_index;

	C2_PRE(buf != NULL);

	ctx = cb_ctx_extract(ev);
	C2_ASSERT(ctx != NULL);
	buf_index = cb_buf_index_extract(ev, ctx, q);

	/* c2_net_buffer.nb_max_receive_msgs will be always set to 1 */
	if (q == C2_NET_QT_MSG_RECV || q == C2_NET_QT_ACTIVE_BULK_RECV ||
	    q == C2_NET_QT_PASSIVE_BULK_RECV) {
		buf->nb_length = ev->nbe_length;
		buf->nb_offset = 0;
	}

	ctx->ntc_buf_cb.ntnbc_cb[q](ctx, buf_index, q, ev);
}

static void cb_msg_recv(const struct c2_net_buffer_event *ev)
{
	cb_default(ev, C2_NET_QT_MSG_RECV);
}

static void cb_msg_send(const struct c2_net_buffer_event *ev)
{
	cb_default(ev, C2_NET_QT_MSG_SEND);
}

static void cb_active_send(const struct c2_net_buffer_event *ev)
{
	cb_default(ev, C2_NET_QT_ACTIVE_BULK_SEND);
}

static void cb_passive_send(const struct c2_net_buffer_event *ev)
{
	cb_default(ev, C2_NET_QT_PASSIVE_BULK_SEND);
}

static void cb_active_recv(const struct c2_net_buffer_event *ev)
{
	cb_default(ev, C2_NET_QT_ACTIVE_BULK_RECV);
}

static void cb_passive_recv(const struct c2_net_buffer_event *ev)
{
	cb_default(ev, C2_NET_QT_PASSIVE_BULK_RECV);
}

static struct c2_net_buffer_callbacks net_test_network_buf_cb = {
	.nbc_cb = {
		[C2_NET_QT_MSG_RECV]		= cb_msg_recv,
		[C2_NET_QT_MSG_SEND]		= cb_msg_send,
		[C2_NET_QT_PASSIVE_BULK_RECV]	= cb_passive_recv,
		[C2_NET_QT_PASSIVE_BULK_SEND]	= cb_passive_send,
		[C2_NET_QT_ACTIVE_BULK_RECV]	= cb_active_recv,
		[C2_NET_QT_ACTIVE_BULK_SEND]	= cb_active_send,
	}
};

/**
   Initialize network buffer with given size
   (allocate and register within domain).
   @see cb_default()
   @pre buf != NULL
 */
static int net_test_buf_init(struct c2_net_buffer *buf,
			     c2_bcount_t size,
			     struct c2_net_test_network_ctx *ctx)
{
	int		      rc;
	c2_bcount_t	      seg_size;
	uint32_t	      seg_num;
	c2_bcount_t	      seg_size_max;
	uint32_t	      seg_num_max;
	c2_bcount_t	      buf_size_max;
	struct c2_net_domain *dom;

	C2_PRE(buf != NULL);
	C2_PRE(ctx != NULL);

	C2_SET0(buf);

	dom = &ctx->ntc_dom;

	buf_size_max = c2_net_domain_get_max_buffer_size(dom);
	if (size > buf_size_max)
		return -E2BIG;

	seg_num_max  = c2_net_domain_get_max_buffer_segments(dom);
	seg_size_max = c2_net_domain_get_max_buffer_segment_size(dom);

	C2_ASSERT(seg_size_max > 0);

	seg_size = size < seg_size_max ? size : seg_size_max;
	seg_num  = size / seg_size_max + !!(size % seg_size_max);

	if (seg_size * seg_num > buf_size_max)
		return -E2BIG;

	rc = c2_bufvec_alloc_aligned(&buf->nb_buffer, seg_num, seg_size,
			C2_SEG_SHIFT);
	if (rc == 0) {
		buf->nb_length		 = size;
		buf->nb_max_receive_msgs = 1;
		buf->nb_min_receive_size = size;
		buf->nb_offset	         = 0;
		buf->nb_callbacks	 = &net_test_network_buf_cb;
		buf->nb_ep		 = NULL;
		buf->nb_desc.nbd_len	 = 0;
		buf->nb_desc.nbd_data	 = NULL;
		buf->nb_app_private	 = ctx;
		buf->nb_timeout		 = C2_TIME_NEVER;

		rc = c2_net_buffer_register(buf, dom);
		if (rc != 0)
			c2_bufvec_free_aligned(&buf->nb_buffer,
					C2_SEG_SHIFT);
	}
	return rc;
}

static void net_test_buf_fini(struct c2_net_buffer *buf,
			      struct c2_net_domain *dom)
{
	C2_PRE(buf->nb_dom == dom);

	c2_net_buffer_deregister(buf, dom);
	c2_bufvec_free_aligned(&buf->nb_buffer, C2_SEG_SHIFT);
	c2_net_desc_free(&buf->nb_desc);
}

static void net_test_bufs_fini(struct c2_net_buffer *buf,
			       uint32_t buf_nr,
			       struct c2_net_domain *dom)
{
	int i;

	for (i = 0; i < buf_nr; ++i)
		net_test_buf_fini(&buf[i], dom);
}

static int net_test_bufs_init(struct c2_net_buffer *buf,
			      uint32_t buf_nr,
			      c2_bcount_t size,
			      struct c2_net_test_network_ctx *ctx)
{
	int		      i;
	int		      rc = 0;
	struct c2_net_domain *dom = &ctx->ntc_dom;

	for (i = 0; i < buf_nr; ++i) {
		rc = net_test_buf_init(&buf[i], size, ctx);
		if (rc != 0)
			break;
		C2_ASSERT(buf[i].nb_dom == dom);
	}
	if (i != buf_nr)
		net_test_bufs_fini(buf, i, dom);
	return rc;
}

/** Stop transfer machine and wait for state transition */
static void net_test_tm_stop(struct c2_net_transfer_mc *tm)
{
	int	        rc;
	struct c2_clink	tmwait;

	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&tm->ntm_chan, &tmwait);

	rc = c2_net_tm_stop(tm, true);
	C2_ASSERT(rc == 0);

	do {
		c2_chan_wait(&tmwait);
	} while (tm->ntm_state != C2_NET_TM_STOPPED &&
		 tm->ntm_state != C2_NET_TM_FAILED);

	c2_clink_del(&tmwait);
	c2_clink_fini(&tmwait);
}

bool c2_net_test_network_ctx_invariant(struct c2_net_test_network_ctx *ctx)
{
	C2_PRE(ctx != NULL);

	return ctx->ntc_ep_nr <= ctx->ntc_ep_max;
}

/* @todo rearrange to allocate memory before initializing network structs? */
int c2_net_test_network_ctx_init(struct c2_net_test_network_ctx *ctx,
				 const char *tm_addr,
				 const struct c2_net_tm_callbacks *tm_cb,
				 const struct
				 c2_net_test_network_buffer_callbacks *buf_cb,
				 c2_bcount_t buf_size_ping,
				 uint32_t buf_ping_nr,
				 c2_bcount_t buf_size_bulk,
				 uint32_t buf_bulk_nr,
				 uint32_t ep_max,
				 const struct c2_net_test_network_timeouts
				 *timeouts)
{
	int		       rc;
	static struct c2_clink tmwait;

	C2_PRE(ctx     != NULL);
	C2_PRE(tm_addr != NULL);
	C2_PRE(tm_cb   != NULL);
	C2_PRE(buf_cb  != NULL);

	C2_SET0(ctx);

	rc = c2_net_domain_init(&ctx->ntc_dom, &c2_net_lnet_xprt);
	if (rc != 0)
		return rc;

	ctx->ntc_tm_cb	     = *tm_cb;
	ctx->ntc_buf_cb	     = *buf_cb;
	ctx->ntc_buf_ping_nr = buf_ping_nr;
	ctx->ntc_buf_bulk_nr = buf_bulk_nr;
	ctx->ntc_ep_nr	     = 0;
	ctx->ntc_ep_max	     = ep_max;
	ctx->ntc_timeouts    = timeouts != NULL ? *timeouts :
			       c2_net_test_network_timeouts_never();

	/* init and start tm */
	ctx->ntc_tm.ntm_state     = C2_NET_TM_UNDEFINED;
	ctx->ntc_tm.ntm_callbacks = &ctx->ntc_tm_cb;


	rc = c2_net_tm_init(&ctx->ntc_tm, &ctx->ntc_dom);
	if (rc != 0)
		goto fini_dom;

	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&ctx->ntc_tm.ntm_chan, &tmwait);
	rc = c2_net_tm_start(&ctx->ntc_tm, tm_addr);
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	c2_clink_fini(&tmwait);
	if (rc != 0)
		goto fini_tm;
	rc = -ECONNREFUSED;
	if (ctx->ntc_tm.ntm_state != C2_NET_TM_STARTED)
		goto fini_tm;

	rc = -ENOMEM;
	/* alloc arrays */
	C2_ALLOC_ARR(ctx->ntc_buf_ping, ctx->ntc_buf_ping_nr);
	if (ctx->ntc_buf_ping == NULL)
		goto stop_tm;
	C2_ALLOC_ARR(ctx->ntc_buf_bulk, ctx->ntc_buf_bulk_nr);
	if (ctx->ntc_buf_bulk == NULL)
		goto free_buf_bulk;
	C2_ALLOC_ARR(ctx->ntc_ep, ctx->ntc_ep_max);
	if (ctx->ntc_buf_bulk == NULL)
		goto free_buf_ping;

	/* init buffers */
	rc = net_test_bufs_init(ctx->ntc_buf_ping, ctx->ntc_buf_ping_nr,
			buf_size_ping, ctx);
	if (rc != 0)
		goto free_ep;
	rc = net_test_bufs_init(ctx->ntc_buf_bulk, ctx->ntc_buf_bulk_nr,
			buf_size_bulk, ctx);
	if (rc != 0)
		goto free_bufs_ping;

	C2_POST(c2_net_test_network_ctx_invariant(ctx));
	goto success;

    free_bufs_ping:
	net_test_bufs_fini(ctx->ntc_buf_ping, ctx->ntc_buf_ping_nr,
			&ctx->ntc_dom);
    free_ep:
	c2_free(ctx->ntc_ep);
    free_buf_bulk:
	c2_free(ctx->ntc_buf_bulk);
    free_buf_ping:
	c2_free(ctx->ntc_buf_ping);
    stop_tm:
	net_test_tm_stop(&ctx->ntc_tm);
    fini_tm:
	c2_net_tm_fini(&ctx->ntc_tm);
    fini_dom:
	c2_net_domain_fini(&ctx->ntc_dom);
    success:
	return rc;
}

void c2_net_test_network_ctx_fini(struct c2_net_test_network_ctx *ctx)
{
	int i;

	C2_PRE(c2_net_test_network_ctx_invariant(ctx));

	for (i = 0; i < ctx->ntc_ep_nr; ++i)
		c2_net_end_point_put(ctx->ntc_ep[i]);
	net_test_bufs_fini(ctx->ntc_buf_bulk, ctx->ntc_buf_bulk_nr,
			&ctx->ntc_dom);
	net_test_bufs_fini(ctx->ntc_buf_ping, ctx->ntc_buf_ping_nr,
			&ctx->ntc_dom);
	c2_free(ctx->ntc_ep);
	c2_free(ctx->ntc_buf_bulk);
	c2_free(ctx->ntc_buf_ping);
	net_test_tm_stop(&ctx->ntc_tm);
	c2_net_tm_fini(&ctx->ntc_tm);
	c2_net_domain_fini(&ctx->ntc_dom);
}

int c2_net_test_network_ep_add(struct c2_net_test_network_ctx *ctx,
			       const char *ep_addr)
{
	int rc;

	C2_PRE(c2_net_test_network_ctx_invariant(ctx));
	C2_PRE(ep_addr != NULL);

	if (ctx->ntc_ep_nr != ctx->ntc_ep_max) {
		rc = c2_net_end_point_create(&ctx->ntc_ep[ctx->ntc_ep_nr],
				&ctx->ntc_tm, ep_addr);
		C2_ASSERT(rc <= 0);
		if (rc == 0)
			rc = ctx->ntc_ep_nr++;
	} else {
		rc = -E2BIG;
	}
	return rc;
}

static int net_test_buf_queue(struct c2_net_test_network_ctx *ctx,
			      struct c2_net_buffer *nb,
			      enum c2_net_queue_type q)
{
	c2_time_t timeout = ctx->ntc_timeouts.ntnt_timeout[q];

	C2_PRE((nb->nb_flags & C2_NET_BUF_QUEUED) == 0);
	C2_PRE(ergo(q == C2_NET_QT_MSG_SEND, nb->nb_ep != NULL));

	nb->nb_qtype   = q;
	nb->nb_offset  = 0;	/* nb->nb_length already set */
	nb->nb_ep      = q != C2_NET_QT_MSG_SEND ? NULL : nb->nb_ep;
	nb->nb_timeout = timeout == C2_TIME_NEVER ?
			 C2_TIME_NEVER : c2_time_add(c2_time_now(), timeout);

	return c2_net_buffer_add(nb, &ctx->ntc_tm);
}

int c2_net_test_network_msg_send(struct c2_net_test_network_ctx *ctx,
				 uint32_t buf_ping_index,
				 uint32_t ep_index)
{
	struct c2_net_buffer *nb;

	C2_PRE(c2_net_test_network_ctx_invariant(ctx));
	C2_PRE(buf_ping_index < ctx->ntc_buf_ping_nr);
	C2_PRE(ep_index < ctx->ntc_ep_nr);

	nb = &ctx->ntc_buf_ping[buf_ping_index];
	nb->nb_ep = ctx->ntc_ep[ep_index];

	return net_test_buf_queue(ctx, nb, C2_NET_QT_MSG_SEND);
}

int c2_net_test_network_msg_recv(struct c2_net_test_network_ctx *ctx,
				 uint32_t buf_ping_index)
{
	C2_PRE(c2_net_test_network_ctx_invariant(ctx));
	C2_PRE(buf_ping_index < ctx->ntc_buf_ping_nr);

	return net_test_buf_queue(ctx, &ctx->ntc_buf_ping[buf_ping_index],
			C2_NET_QT_MSG_RECV);
}

int c2_net_test_network_bulk_enqueue(struct c2_net_test_network_ctx *ctx,
				     int32_t buf_bulk_index,
				     int32_t ep_index,
				     enum c2_net_queue_type q)
{
	struct c2_net_buffer *buf;

	C2_PRE(c2_net_test_network_ctx_invariant(ctx));
	C2_PRE(buf_bulk_index < ctx->ntc_buf_bulk_nr);

	buf = &ctx->ntc_buf_bulk[buf_bulk_index];
	if (q == C2_NET_QT_PASSIVE_BULK_SEND ||
			q == C2_NET_QT_PASSIVE_BULK_RECV) {

		C2_PRE(ep_index < ctx->ntc_ep_nr);
		buf->nb_ep = ctx->ntc_ep[ep_index];
	} else
		buf->nb_ep = NULL;

	return net_test_buf_queue(ctx, buf, q);
}

void c2_net_test_network_buffer_dequeue(struct c2_net_test_network_ctx *ctx,
					enum c2_net_test_network_buf_type
					buf_type,
					int32_t buf_index)
{
	C2_PRE(c2_net_test_network_ctx_invariant(ctx));
	c2_net_buffer_del(c2_net_test_network_buf(ctx, buf_type, buf_index),
			&ctx->ntc_tm);
}

void c2_net_test_network_bd_reset(struct c2_net_test_network_ctx *ctx,
				  int32_t buf_ping_index)
{
	C2_PRE(c2_net_test_network_ctx_invariant(ctx));

	ctx->ntc_buf_ping[buf_ping_index].nb_length = 0;
}

static c2_bcount_t bufvec_append(struct c2_bufvec_cursor *dcur,
				 void *data,
				 c2_bcount_t length)
{
	struct c2_bufvec	data_bv = C2_BUFVEC_INIT_BUF(&data, &length);
	struct c2_bufvec_cursor data_cur;

	c2_bufvec_cursor_init(&data_cur, &data_bv);
	return c2_bufvec_cursor_copy(dcur, &data_cur, length);
}

static c2_bcount_t bufvec_read(struct c2_bufvec_cursor *scur,
			       void *data,
			       c2_bcount_t length)
{
	struct c2_bufvec        data_bv = C2_BUFVEC_INIT_BUF(&data, &length);
	struct c2_bufvec_cursor data_cur;

	c2_bufvec_cursor_init(&data_cur, &data_bv);
	return c2_bufvec_cursor_copy(&data_cur, scur, length);
}

static bool buf_desc_decode(struct c2_bufvec_cursor *cur,
			    c2_bcount_t offset,
			    c2_bcount_t buf_len,
			    c2_bcount_t *passive_len,
			    int32_t *desc_len)
{
	char	    str[C2_NET_TEST_STRLEN_NBD_HEADER];
	c2_bcount_t rc_bcount;
	int	    rc;

	if (offset + C2_NET_TEST_STRLEN_NBD_HEADER > buf_len)
		return false;

	rc_bcount = bufvec_read(cur, str, C2_NET_TEST_STRLEN_NBD_HEADER);
	if (rc_bcount != C2_NET_TEST_STRLEN_NBD_HEADER)
		return false;

	/* note Linux uses the LP64 standard */
	rc = sscanf(str, "%lu %u",
		    (unsigned long *) passive_len, desc_len);
#ifdef DEBUG_NET_TEST_NETWORK
	printf("rc = %d, passive_len = %"PRIu64", desc_len = %"PRIu32", "
		"str = %s\n", rc, *passive_len, *desc_len, str);
#endif
	return rc == 2 &&
		offset + C2_NET_TEST_STRLEN_NBD_HEADER + *desc_len <= buf_len;
}

uint32_t c2_net_test_network_bd_count(struct c2_net_test_network_ctx *ctx,
				      int32_t buf_ping_index)
{
	struct c2_net_buffer   *buf_ping;
	struct c2_bufvec_cursor cur_buf;
	uint32_t		result = 0;
	c2_bcount_t		offset;
	c2_bcount_t		buf_len;
	c2_bcount_t		passive_len;
	int32_t			desc_len;

	C2_PRE(c2_net_test_network_ctx_invariant(ctx));
	C2_PRE(buf_ping_index < ctx->ntc_buf_ping_nr);

	buf_ping = &ctx->ntc_buf_ping[buf_ping_index];
	c2_bufvec_cursor_init(&cur_buf, &buf_ping->nb_buffer);
	c2_bufvec_cursor_move(&cur_buf,  buf_ping->nb_offset);

	offset  = 0;
	buf_len = buf_ping->nb_length;
	while (offset < buf_len) {
		if (!buf_desc_decode(&cur_buf, offset, buf_len, &passive_len,
				     &desc_len))
			break;
		result++;
		c2_bufvec_cursor_move(&cur_buf, desc_len);
		offset += C2_NET_TEST_STRLEN_NBD_HEADER + desc_len;
	}

	return result;
}

int c2_net_test_network_bd_encode(struct c2_net_test_network_ctx *ctx,
				  int32_t buf_ping_index,
				  int32_t buf_bulk_index)
{
	int			rc;
	c2_bcount_t		rc_bcount;
	struct c2_net_buffer   *buf_bulk;
	struct c2_net_buffer   *buf_ping;
	struct c2_bufvec_cursor cur_buf;
	c2_bcount_t	        desc_len;
	const c2_bcount_t	str_len = 20 + 1 + 10 + 1;
	/*				  ^    ^   ^    ^
		passive side buffer size -+   ' '  |  '\0'
		c2_net_buf_desc .nbd_len ----------+
	 */
	char			str[str_len];

	C2_CASSERT(str_len == C2_NET_TEST_STRLEN_NBD_HEADER);
	C2_PRE(c2_net_test_network_ctx_invariant(ctx));
	C2_PRE(buf_bulk_index < ctx->ntc_buf_bulk_nr);
	C2_PRE(buf_ping_index < ctx->ntc_buf_ping_nr);

	buf_bulk = &ctx->ntc_buf_bulk[buf_bulk_index];
	buf_ping = &ctx->ntc_buf_ping[buf_ping_index];

	desc_len = buf_bulk->nb_desc.nbd_len;

	c2_bufvec_cursor_init(&cur_buf, &buf_ping->nb_buffer);
	c2_bufvec_cursor_move(&cur_buf, buf_ping->nb_offset +
					buf_ping->nb_length);

	/* check for space left in buf_ping */
	if (c2_vec_count(&buf_ping->nb_buffer.ov_vec) <
	    buf_ping->nb_offset + buf_ping->nb_length + str_len + desc_len)
		return -E2BIG;

	/* fill str[] */
	/* note Linux uses the LP64 standard */
	rc = snprintf(str, str_len, "%020lu %010u",
		      (unsigned long) buf_bulk->nb_length,
		      buf_bulk->nb_desc.nbd_len);
	C2_ASSERT(rc == str_len - 1);
	/* copy str[] to buf_ping */
	rc_bcount = bufvec_append(&cur_buf, str, str_len);
	if (rc_bcount != str_len)
		return -E2BIG;
	/* copy c2_net_buf_desc .nbd_data to buf_ping */
	rc_bcount = bufvec_append(&cur_buf, buf_bulk->nb_desc.nbd_data,
				  desc_len);
	if (rc_bcount != desc_len)
		return -E2BIG;
	/* adjust buf_ping .nb_length */
	buf_ping->nb_length += str_len + desc_len;
	return 0;
}

/* see c2_net_test_network_bd_encode() */
int c2_net_test_network_bd_decode(struct c2_net_test_network_ctx *ctx,
				  int32_t buf_ping_index,
				  int32_t buf_bulk_index)
{
	struct c2_net_buffer   *buf_bulk;
	struct c2_net_buffer   *buf_ping;
	struct c2_bufvec_cursor cur_buf;
	c2_bcount_t		buf_ping_len;
	c2_bcount_t		passive_len;
	c2_bcount_t		offset;
	int32_t			desc_len;
	c2_bcount_t		rc_bcount;

	C2_PRE(c2_net_test_network_ctx_invariant(ctx));
	C2_PRE(buf_bulk_index < ctx->ntc_buf_bulk_nr);
	C2_PRE(buf_ping_index < ctx->ntc_buf_ping_nr);

	buf_bulk = &ctx->ntc_buf_bulk[buf_bulk_index];
	buf_ping = &ctx->ntc_buf_ping[buf_ping_index];

	c2_bufvec_cursor_init(&cur_buf, &buf_ping->nb_buffer);
	c2_bufvec_cursor_move(&cur_buf, buf_ping->nb_offset +
					buf_ping->nb_length);

	offset  = buf_ping->nb_offset + buf_ping->nb_length;
	buf_ping_len = c2_vec_count(&buf_ping->nb_buffer.ov_vec);
	if (!buf_desc_decode(&cur_buf,
			offset, buf_ping_len, &passive_len, &desc_len))
		return -EBADMSG;

	if (passive_len > c2_vec_count(&buf_bulk->nb_buffer.ov_vec))
		return -E2BIG;

	/* optimizing memory allocation */
	if (buf_bulk->nb_desc.nbd_len != desc_len) {
		/* free old */
		c2_free(buf_bulk->nb_desc.nbd_data);
		buf_bulk->nb_desc.nbd_len = 0;
		/* alloc new */
		buf_bulk->nb_desc.nbd_data = c2_alloc(desc_len);
		if (buf_bulk->nb_desc.nbd_data == NULL)
			return -ENOMEM;
		buf_bulk->nb_desc.nbd_len = desc_len;
	}

	rc_bcount = bufvec_read(&cur_buf, buf_bulk->nb_desc.nbd_data, desc_len);
	if (rc_bcount != desc_len)
		return -EBADMSG;

	buf_ping->nb_length += C2_NET_TEST_STRLEN_NBD_HEADER + desc_len;
	return 0;
}

struct c2_net_buffer *
c2_net_test_network_buf(struct c2_net_test_network_ctx *ctx,
			enum c2_net_test_network_buf_type buf_type,
			uint32_t buf_index)
{
	C2_PRE(ctx != NULL);
	C2_PRE(buf_type == C2_NET_TEST_BUF_PING ||
	       buf_type == C2_NET_TEST_BUF_BULK);
	C2_PRE(buf_index < (buf_type == C2_NET_TEST_BUF_PING ?
	       ctx->ntc_buf_ping_nr : ctx->ntc_buf_bulk_nr));

	return buf_type == C2_NET_TEST_BUF_PING ?
		&ctx->ntc_buf_ping[buf_index] : &ctx->ntc_buf_bulk[buf_index];
}

int c2_net_test_network_buf_resize(struct c2_net_test_network_ctx *ctx,
				   enum c2_net_test_network_buf_type buf_type,
				   uint32_t buf_index,
				   c2_bcount_t new_size) {
	struct c2_net_buffer *buf;

	C2_PRE(ctx != NULL);

	buf = c2_net_test_network_buf(ctx, buf_type, buf_index);
	C2_ASSERT(buf != NULL);

	net_test_buf_fini(buf, &ctx->ntc_dom);
	return net_test_buf_init(buf, new_size, ctx);
}

void c2_net_test_network_buf_fill(struct c2_net_test_network_ctx *ctx,
				  enum c2_net_test_network_buf_type buf_type,
				  uint32_t buf_index,
				  uint8_t fill)
{
	struct c2_bufvec       *bv;
	struct c2_bufvec_cursor bc;
	c2_bcount_t		length;
	c2_bcount_t		i;
	bool			rc_bool;

	C2_PRE(c2_net_test_network_ctx_invariant(ctx));

	bv = &c2_net_test_network_buf(ctx, buf_type, buf_index)->nb_buffer;
	C2_ASSERT(bv != NULL);
	length = c2_vec_count(&bv->ov_vec);
	c2_bufvec_cursor_init(&bc, bv);
	/* @todo use c2_bufvec_cursor_step */
	for (i = 0; i < length; ++i) {
		* (uint8_t *) c2_bufvec_cursor_addr(&bc) = fill;
		c2_bufvec_cursor_move(&bc, 1);
	}
	rc_bool = c2_bufvec_cursor_move(&bc, 0);
	C2_ASSERT(rc_bool);
}

struct c2_net_test_network_timeouts c2_net_test_network_timeouts_never(void)
{
	struct c2_net_test_network_timeouts result;
	int				    i;

	for (i = 0; i < C2_NET_QT_NR; ++i)
		result.ntnt_timeout[i] = C2_TIME_NEVER;
	return result;
}

/**
   @} end NetTestNetworkInternals
 */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
