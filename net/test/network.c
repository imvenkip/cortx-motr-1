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

#ifdef __KERNEL__
#include <linux/kernel.h>	/* snprintf() */
#else
#include <stdio.h>		/* snprintf() */
#include <inttypes.h>		/* PRIu64 */
#endif

#include "lib/cdefs.h"		/* ergo */
#include "lib/errno.h"		/* E2BIG */
#include "lib/memory.h"		/* M0_ALLOC_ARR */
#include "lib/misc.h"		/* M0_SET0 */
#include "lib/vec.h"		/* M0_SEG_SHIFT */

#include "net/net.h"
#include "net/lnet/lnet.h"	/* m0_net_lnet_xprt */

#include "net/test/network.h"

/**
   @defgroup NetTestNetworkInternals Network
   @ingroup NetTestInternals

   @todo add timeouts to channels and network buffers
   @todo align code (function parameters etc.)
   @todo cache m0_vec_count()

   @see
   @ref net-test

   @{
 */

#if 0
#ifndef __KERNEL__
#define DEBUG_NET_TEST_NETWORK
#endif
#endif

/* see buf_desc_decode() and m0_net_test_network_bd_encode() */
M0_BASSERT(sizeof(unsigned)	 == sizeof(uint32_t));
M0_BASSERT(sizeof(unsigned long) == sizeof(m0_bcount_t));

enum {
	M0_NET_TEST_STRLEN_NBD_HEADER = 32,
};

int m0_net_test_network_init(void)
{
	return m0_net_xprt_init(&m0_net_lnet_xprt);
}

void m0_net_test_network_fini(void)
{
	m0_net_xprt_fini(&m0_net_lnet_xprt);
}

/**
   Get net-test network context for the buffer event.
 */
static struct m0_net_test_network_ctx *
cb_ctx_extract(const struct m0_net_buffer_event *ev)
{
	return ev->nbe_buffer->nb_app_private;
}

/**
   Get buffer number in net-test network context for the buffer event.
 */
static uint32_t cb_buf_index_extract(const struct m0_net_buffer_event *ev,
				     struct m0_net_test_network_ctx *ctx,
				     enum m0_net_queue_type q)
{
	struct m0_net_buffer *arr;
	bool		      type_ping;
	int		      index;
	int		      index_max;

	M0_PRE(ctx != NULL);

	type_ping = q == M0_NET_QT_MSG_SEND || q == M0_NET_QT_MSG_RECV;
	arr = type_ping ? ctx->ntc_buf_ping : ctx->ntc_buf_bulk;
	index = ev->nbe_buffer - arr;
	index_max = type_ping ? ctx->ntc_buf_ping_nr : ctx->ntc_buf_bulk_nr;

	M0_POST(index >= 0 && index < index_max);

	return index;
}

/**
   Default callback for all network buffers.
   Calls user-defined callback for the buffer.
   @see net_test_buf_init()
 */
static void cb_default(const struct m0_net_buffer_event *ev)
{
	struct m0_net_buffer	       *buf = ev->nbe_buffer;
	struct m0_net_test_network_ctx *ctx;
	uint32_t			buf_index;
	enum m0_net_queue_type		q;

	M0_PRE(buf != NULL);
	q = ev->nbe_buffer->nb_qtype;

	ctx = cb_ctx_extract(ev);
	M0_ASSERT(ctx != NULL);
	buf_index = cb_buf_index_extract(ev, ctx, q);

	/* m0_net_buffer.nb_max_receive_msgs will be always set to 1 */
	if (q == M0_NET_QT_MSG_RECV || q == M0_NET_QT_ACTIVE_BULK_RECV ||
	    q == M0_NET_QT_PASSIVE_BULK_RECV) {
		buf->nb_length = ev->nbe_length;
		buf->nb_offset = 0;
	}

	ctx->ntc_buf_cb.ntnbc_cb[q](ctx, buf_index, q, ev);
}

static struct m0_net_buffer_callbacks net_test_network_buf_cb = {
	.nbc_cb = {
		[M0_NET_QT_MSG_RECV]		= cb_default,
		[M0_NET_QT_MSG_SEND]		= cb_default,
		[M0_NET_QT_PASSIVE_BULK_RECV]	= cb_default,
		[M0_NET_QT_PASSIVE_BULK_SEND]	= cb_default,
		[M0_NET_QT_ACTIVE_BULK_RECV]	= cb_default,
		[M0_NET_QT_ACTIVE_BULK_SEND]	= cb_default,
	}
};

/**
   Initialize network buffer with given size
   (allocate and register within domain).
   @see cb_default()
   @pre buf != NULL
 */
static int net_test_buf_init(struct m0_net_buffer *buf,
			     m0_bcount_t size,
			     struct m0_net_test_network_ctx *ctx)
{
	int		      rc;
	m0_bcount_t	      seg_size;
	uint32_t	      seg_num;
	m0_bcount_t	      seg_size_max;
	uint32_t	      seg_num_max;
	m0_bcount_t	      buf_size_max;
	struct m0_net_domain *dom;

	M0_PRE(buf != NULL);
	M0_PRE(ctx != NULL);

	M0_SET0(buf);

	dom = ctx->ntc_dom;

	buf_size_max = m0_net_domain_get_max_buffer_size(dom);
	if (size > buf_size_max)
		return -E2BIG;

	seg_num_max  = m0_net_domain_get_max_buffer_segments(dom);
	seg_size_max = m0_net_domain_get_max_buffer_segment_size(dom);

	M0_ASSERT(seg_size_max > 0);

	seg_size = size < seg_size_max ? size : seg_size_max;
	seg_num  = size / seg_size_max + !!(size % seg_size_max);

	if (seg_size * seg_num > buf_size_max)
		return -E2BIG;

	rc = m0_bufvec_alloc_aligned(&buf->nb_buffer, seg_num, seg_size,
			M0_SEG_SHIFT);
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
		buf->nb_timeout		 = M0_TIME_NEVER;

		rc = m0_net_buffer_register(buf, dom);
		if (rc != 0)
			m0_bufvec_free_aligned(&buf->nb_buffer,
					M0_SEG_SHIFT);
	}
	return rc;
}

static void net_test_buf_fini(struct m0_net_buffer *buf,
			      struct m0_net_domain *dom)
{
	M0_PRE(buf->nb_dom == dom);

	m0_net_buffer_deregister(buf, dom);
	m0_bufvec_free_aligned(&buf->nb_buffer, M0_SEG_SHIFT);
	m0_net_desc_free(&buf->nb_desc);
}

static void net_test_bufs_fini(struct m0_net_buffer *buf,
			       uint32_t buf_nr,
			       struct m0_net_domain *dom)
{
	int i;

	for (i = 0; i < buf_nr; ++i)
		net_test_buf_fini(&buf[i], dom);
}

static int net_test_bufs_init(struct m0_net_buffer *buf,
			      uint32_t buf_nr,
			      m0_bcount_t size,
			      struct m0_net_test_network_ctx *ctx)
{
	int		      i;
	int		      rc = 0;
	struct m0_net_domain *dom = ctx->ntc_dom;

	for (i = 0; i < buf_nr; ++i) {
		rc = net_test_buf_init(&buf[i], size, ctx);
		if (rc != 0)
			break;
		M0_ASSERT(buf[i].nb_dom == dom);
	}
	if (i != buf_nr)
		net_test_bufs_fini(buf, i, dom);
	return rc;
}

/** Stop transfer machine and wait for state transition */
static void net_test_tm_stop(struct m0_net_transfer_mc *tm)
{
	int	        rc;
	struct m0_clink	tmwait;

	m0_clink_init(&tmwait, NULL);
	m0_clink_add(&tm->ntm_chan, &tmwait);

	rc = m0_net_tm_stop(tm, true);
	M0_ASSERT(rc == 0);

	do {
		m0_chan_wait(&tmwait);
	} while (tm->ntm_state != M0_NET_TM_STOPPED &&
		 tm->ntm_state != M0_NET_TM_FAILED);

	m0_clink_del(&tmwait);
	m0_clink_fini(&tmwait);
}

bool m0_net_test_network_ctx_invariant(struct m0_net_test_network_ctx *ctx)
{
	M0_PRE(ctx != NULL);

	return ctx->ntc_ep_nr <= ctx->ntc_ep_max;
}

int m0_net_test_network_ctx_init(struct m0_net_test_network_ctx *ctx,
				 const char *tm_addr,
				 const struct m0_net_tm_callbacks *tm_cb,
				 const struct
				 m0_net_test_network_buffer_callbacks *buf_cb,
				 m0_bcount_t buf_size_ping,
				 uint32_t buf_ping_nr,
				 m0_bcount_t buf_size_bulk,
				 uint32_t buf_bulk_nr,
				 uint32_t ep_max,
				 const struct m0_net_test_network_timeouts
				 *timeouts)
{
	int		       rc;
	struct m0_clink        tmwait;

	M0_PRE(ctx     != NULL);
	M0_PRE(tm_addr != NULL);
	M0_PRE(tm_cb   != NULL);
	M0_PRE(buf_cb  != NULL);

	M0_SET0(ctx);

	M0_ALLOC_PTR(ctx->ntc_dom);
	if (ctx->ntc_dom == NULL)
		return -ENOMEM;

	rc = m0_net_domain_init(ctx->ntc_dom, &m0_net_lnet_xprt);
	if (rc != 0)
		goto free_dom;

	ctx->ntc_tm_cb	     = *tm_cb;
	ctx->ntc_buf_cb	     = *buf_cb;
	ctx->ntc_buf_ping_nr = buf_ping_nr;
	ctx->ntc_buf_bulk_nr = buf_bulk_nr;
	ctx->ntc_ep_nr	     = 0;
	ctx->ntc_ep_max	     = ep_max;
	ctx->ntc_timeouts    = timeouts != NULL ? *timeouts :
			       m0_net_test_network_timeouts_never();

	/* init and start tm */
	M0_ALLOC_PTR(ctx->ntc_tm);
	if (ctx->ntc_tm == NULL)
		goto fini_dom;

	ctx->ntc_tm->ntm_state     = M0_NET_TM_UNDEFINED;
	ctx->ntc_tm->ntm_callbacks = &ctx->ntc_tm_cb;


	rc = m0_net_tm_init(ctx->ntc_tm, ctx->ntc_dom);
	if (rc != 0)
		goto free_tm;

	m0_clink_init(&tmwait, NULL);
	m0_clink_add(&ctx->ntc_tm->ntm_chan, &tmwait);
	rc = m0_net_tm_start(ctx->ntc_tm, tm_addr);
	m0_chan_wait(&tmwait);
	m0_clink_del(&tmwait);
	m0_clink_fini(&tmwait);
	if (rc != 0)
		goto fini_tm;
	rc = -ECONNREFUSED;
	if (ctx->ntc_tm->ntm_state != M0_NET_TM_STARTED)
		goto fini_tm;

	rc = -ENOMEM;
	/* alloc arrays */
	M0_ALLOC_ARR(ctx->ntc_buf_ping, ctx->ntc_buf_ping_nr);
	if (ctx->ntc_buf_ping == NULL)
		goto stop_tm;
	M0_ALLOC_ARR(ctx->ntc_buf_bulk, ctx->ntc_buf_bulk_nr);
	if (ctx->ntc_buf_bulk == NULL)
		goto free_buf_bulk;
	M0_ALLOC_ARR(ctx->ntc_ep, ctx->ntc_ep_max);
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

	M0_POST(m0_net_test_network_ctx_invariant(ctx));
	goto success;

    free_bufs_ping:
	net_test_bufs_fini(ctx->ntc_buf_ping, ctx->ntc_buf_ping_nr,
			   ctx->ntc_dom);
    free_ep:
	m0_free(ctx->ntc_ep);
    free_buf_bulk:
	m0_free(ctx->ntc_buf_bulk);
    free_buf_ping:
	m0_free(ctx->ntc_buf_ping);
    stop_tm:
	net_test_tm_stop(ctx->ntc_tm);
    fini_tm:
	m0_net_tm_fini(ctx->ntc_tm);
    free_tm:
	m0_free(ctx->ntc_tm);
    fini_dom:
	m0_net_domain_fini(ctx->ntc_dom);
    free_dom:
	m0_free(ctx->ntc_dom);
    success:
	return rc;
}

void m0_net_test_network_ctx_fini(struct m0_net_test_network_ctx *ctx)
{
	int i;

	M0_PRE(m0_net_test_network_ctx_invariant(ctx));

	for (i = 0; i < ctx->ntc_ep_nr; ++i)
		m0_net_end_point_put(ctx->ntc_ep[i]);
	net_test_bufs_fini(ctx->ntc_buf_bulk, ctx->ntc_buf_bulk_nr,
			   ctx->ntc_dom);
	net_test_bufs_fini(ctx->ntc_buf_ping, ctx->ntc_buf_ping_nr,
			   ctx->ntc_dom);
	m0_free(ctx->ntc_ep);
	m0_free(ctx->ntc_buf_bulk);
	m0_free(ctx->ntc_buf_ping);
	net_test_tm_stop(ctx->ntc_tm);
	m0_net_tm_fini(ctx->ntc_tm);
	m0_net_domain_fini(ctx->ntc_dom);
	m0_free(ctx->ntc_tm);
	m0_free(ctx->ntc_dom);
}

int m0_net_test_network_ep_add(struct m0_net_test_network_ctx *ctx,
			       const char *ep_addr)
{
	int rc;

	M0_PRE(m0_net_test_network_ctx_invariant(ctx));
	M0_PRE(ep_addr != NULL);

	if (ctx->ntc_ep_nr != ctx->ntc_ep_max) {
		rc = m0_net_end_point_create(&ctx->ntc_ep[ctx->ntc_ep_nr],
					     ctx->ntc_tm, ep_addr);
		M0_ASSERT(rc <= 0);
		if (rc == 0)
			rc = ctx->ntc_ep_nr++;
	} else {
		rc = -E2BIG;
	}
	return rc;
}

static int net_test_buf_queue(struct m0_net_test_network_ctx *ctx,
			      struct m0_net_buffer *nb,
			      enum m0_net_queue_type q)
{
	m0_time_t timeout = ctx->ntc_timeouts.ntnt_timeout[q];

	M0_PRE((nb->nb_flags & M0_NET_BUF_QUEUED) == 0);
	M0_PRE(ergo(q == M0_NET_QT_MSG_SEND, nb->nb_ep != NULL));

	nb->nb_qtype   = q;
	nb->nb_offset  = 0;	/* nb->nb_length already set */
	nb->nb_ep      = q != M0_NET_QT_MSG_SEND ? NULL : nb->nb_ep;
	nb->nb_timeout = timeout == M0_TIME_NEVER ?
			 M0_TIME_NEVER : m0_time_add(m0_time_now(), timeout);

	return m0_net_buffer_add(nb, ctx->ntc_tm);
}

int m0_net_test_network_msg_send_ep(struct m0_net_test_network_ctx *ctx,
				    uint32_t buf_ping_index,
				    struct m0_net_end_point *ep)
{
	struct m0_net_buffer *nb;

	M0_PRE(m0_net_test_network_ctx_invariant(ctx));
	M0_PRE(buf_ping_index < ctx->ntc_buf_ping_nr);

	nb = &ctx->ntc_buf_ping[buf_ping_index];
	nb->nb_ep = ep;

	return net_test_buf_queue(ctx, nb, M0_NET_QT_MSG_SEND);
}

int m0_net_test_network_msg_send(struct m0_net_test_network_ctx *ctx,
				 uint32_t buf_ping_index,
				 uint32_t ep_index)
{
	M0_PRE(m0_net_test_network_ctx_invariant(ctx));
	M0_PRE(buf_ping_index < ctx->ntc_buf_ping_nr);
	M0_PRE(ep_index < ctx->ntc_ep_nr);

	return m0_net_test_network_msg_send_ep(ctx, buf_ping_index,
					       ctx->ntc_ep[ep_index]);
}

int m0_net_test_network_msg_recv(struct m0_net_test_network_ctx *ctx,
				 uint32_t buf_ping_index)
{
	M0_PRE(m0_net_test_network_ctx_invariant(ctx));
	M0_PRE(buf_ping_index < ctx->ntc_buf_ping_nr);

	return net_test_buf_queue(ctx, &ctx->ntc_buf_ping[buf_ping_index],
			M0_NET_QT_MSG_RECV);
}

int m0_net_test_network_bulk_enqueue(struct m0_net_test_network_ctx *ctx,
				     int32_t buf_bulk_index,
				     int32_t ep_index,
				     enum m0_net_queue_type q)
{
	struct m0_net_buffer *buf;

	M0_PRE(m0_net_test_network_ctx_invariant(ctx));
	M0_PRE(buf_bulk_index < ctx->ntc_buf_bulk_nr);

	buf = &ctx->ntc_buf_bulk[buf_bulk_index];
	if (q == M0_NET_QT_PASSIVE_BULK_SEND ||
			q == M0_NET_QT_PASSIVE_BULK_RECV) {

		M0_PRE(ep_index < ctx->ntc_ep_nr);
		buf->nb_ep = ctx->ntc_ep[ep_index];
	} else
		buf->nb_ep = NULL;

	return net_test_buf_queue(ctx, buf, q);
}

void m0_net_test_network_buffer_dequeue(struct m0_net_test_network_ctx *ctx,
					enum m0_net_test_network_buf_type
					buf_type,
					int32_t buf_index)
{
	M0_PRE(m0_net_test_network_ctx_invariant(ctx));
	m0_net_buffer_del(m0_net_test_network_buf(ctx, buf_type, buf_index),
			  ctx->ntc_tm);
}

void m0_net_test_network_bd_reset(struct m0_net_test_network_ctx *ctx,
				  int32_t buf_ping_index)
{
	M0_PRE(m0_net_test_network_ctx_invariant(ctx));

	ctx->ntc_buf_ping[buf_ping_index].nb_length = 0;
}

static m0_bcount_t bufvec_append(struct m0_bufvec_cursor *dcur,
				 void *data,
				 m0_bcount_t length)
{
	struct m0_bufvec	data_bv = M0_BUFVEC_INIT_BUF(&data, &length);
	struct m0_bufvec_cursor data_cur;

	m0_bufvec_cursor_init(&data_cur, &data_bv);
	return m0_bufvec_cursor_copy(dcur, &data_cur, length);
}

static m0_bcount_t bufvec_read(struct m0_bufvec_cursor *scur,
			       void *data,
			       m0_bcount_t length)
{
	struct m0_bufvec        data_bv = M0_BUFVEC_INIT_BUF(&data, &length);
	struct m0_bufvec_cursor data_cur;

	m0_bufvec_cursor_init(&data_cur, &data_bv);
	return m0_bufvec_cursor_copy(&data_cur, scur, length);
}

static bool buf_desc_decode(struct m0_bufvec_cursor *cur,
			    m0_bcount_t offset,
			    m0_bcount_t buf_len,
			    m0_bcount_t *passive_len,
			    int32_t *desc_len)
{
	char	    str[M0_NET_TEST_STRLEN_NBD_HEADER];
	m0_bcount_t rc_bcount;
	int	    rc;

	if (offset + M0_NET_TEST_STRLEN_NBD_HEADER > buf_len)
		return false;

	rc_bcount = bufvec_read(cur, str, M0_NET_TEST_STRLEN_NBD_HEADER);
	if (rc_bcount != M0_NET_TEST_STRLEN_NBD_HEADER)
		return false;

	/* note Linux uses the LP64 standard */
	rc = sscanf(str, "%lu %u",
		    (unsigned long *) passive_len, desc_len);
#ifdef DEBUG_NET_TEST_NETWORK
	printf("rc = %d, passive_len = %"PRIu64", desc_len = %"PRIu32", "
		"str = %s\n", rc, *passive_len, *desc_len, str);
#endif
	return rc == 2 &&
		offset + M0_NET_TEST_STRLEN_NBD_HEADER + *desc_len <= buf_len;
}

uint32_t m0_net_test_network_bd_count(struct m0_net_test_network_ctx *ctx,
				      int32_t buf_ping_index)
{
	struct m0_net_buffer   *buf_ping;
	struct m0_bufvec_cursor cur_buf;
	uint32_t		result = 0;
	m0_bcount_t		offset;
	m0_bcount_t		buf_len;
	m0_bcount_t		passive_len;
	int32_t			desc_len;
	bool			decoded;

	M0_PRE(m0_net_test_network_ctx_invariant(ctx));
	M0_PRE(buf_ping_index < ctx->ntc_buf_ping_nr);

	buf_ping = &ctx->ntc_buf_ping[buf_ping_index];
	m0_bufvec_cursor_init(&cur_buf, &buf_ping->nb_buffer);
	m0_bufvec_cursor_move(&cur_buf,  buf_ping->nb_offset);

	offset  = 0;
	buf_len = buf_ping->nb_length;
	while (offset < buf_len) {
		decoded = buf_desc_decode(&cur_buf, offset, buf_len,
					  &passive_len, &desc_len);
		if (!decoded)
			break;
		result++;
		m0_bufvec_cursor_move(&cur_buf, desc_len);
		offset += M0_NET_TEST_STRLEN_NBD_HEADER + desc_len;
	}

	return result;
}

int m0_net_test_network_bd_encode(struct m0_net_test_network_ctx *ctx,
				  int32_t buf_ping_index,
				  int32_t buf_bulk_index)
{
	int			rc;
	m0_bcount_t		rc_bcount;
	struct m0_net_buffer   *buf_bulk;
	struct m0_net_buffer   *buf_ping;
	struct m0_bufvec_cursor cur_buf;
	m0_bcount_t	        desc_len;
	const m0_bcount_t	str_len = 20 + 1 + 10 + 1;
	/*				  ^    ^   ^    ^
		passive side buffer size -+   ' '  |  '\0'
		m0_net_buf_desc .nbd_len ----------+
	 */
	char			str[str_len];

	M0_ASSERT(str_len == M0_NET_TEST_STRLEN_NBD_HEADER);
	M0_PRE(m0_net_test_network_ctx_invariant(ctx));
	M0_PRE(buf_bulk_index < ctx->ntc_buf_bulk_nr);
	M0_PRE(buf_ping_index < ctx->ntc_buf_ping_nr);

	buf_bulk = &ctx->ntc_buf_bulk[buf_bulk_index];
	buf_ping = &ctx->ntc_buf_ping[buf_ping_index];

	desc_len = buf_bulk->nb_desc.nbd_len;

	m0_bufvec_cursor_init(&cur_buf, &buf_ping->nb_buffer);
	m0_bufvec_cursor_move(&cur_buf, buf_ping->nb_offset +
					buf_ping->nb_length);

	/* check for space left in buf_ping */
	if (m0_vec_count(&buf_ping->nb_buffer.ov_vec) <
	    buf_ping->nb_offset + buf_ping->nb_length + str_len + desc_len)
		return -E2BIG;

	/* fill str[] */
	/* note Linux uses the LP64 standard */
	rc = snprintf(str, str_len, "%020lu %010u",
		      (unsigned long) buf_bulk->nb_length,
		      buf_bulk->nb_desc.nbd_len);
	M0_ASSERT(rc == str_len - 1);
	/* copy str[] to buf_ping */
	rc_bcount = bufvec_append(&cur_buf, str, str_len);
	if (rc_bcount != str_len)
		return -E2BIG;
	/* copy m0_net_buf_desc .nbd_data to buf_ping */
	rc_bcount = bufvec_append(&cur_buf, buf_bulk->nb_desc.nbd_data,
				  desc_len);
	if (rc_bcount != desc_len)
		return -E2BIG;
	/* adjust buf_ping .nb_length */
	buf_ping->nb_length += str_len + desc_len;
	return 0;
}

/* see m0_net_test_network_bd_encode() */
int m0_net_test_network_bd_decode(struct m0_net_test_network_ctx *ctx,
				  int32_t buf_ping_index,
				  int32_t buf_bulk_index)
{
	struct m0_net_buffer   *buf_bulk;
	struct m0_net_buffer   *buf_ping;
	struct m0_bufvec_cursor cur_buf;
	m0_bcount_t		buf_ping_len;
	m0_bcount_t		passive_len;
	m0_bcount_t		offset;
	int32_t			desc_len;
	m0_bcount_t		rc_bcount;
	bool			decoded;

	M0_PRE(m0_net_test_network_ctx_invariant(ctx));
	M0_PRE(buf_bulk_index < ctx->ntc_buf_bulk_nr);
	M0_PRE(buf_ping_index < ctx->ntc_buf_ping_nr);

	buf_bulk = &ctx->ntc_buf_bulk[buf_bulk_index];
	buf_ping = &ctx->ntc_buf_ping[buf_ping_index];

	m0_bufvec_cursor_init(&cur_buf, &buf_ping->nb_buffer);
	m0_bufvec_cursor_move(&cur_buf, buf_ping->nb_offset +
					buf_ping->nb_length);

	offset  = buf_ping->nb_offset + buf_ping->nb_length;
	buf_ping_len = m0_vec_count(&buf_ping->nb_buffer.ov_vec);
	decoded = buf_desc_decode(&cur_buf, offset, buf_ping_len,
				  &passive_len, &desc_len);
	if (!decoded)
		return -EBADMSG;

	if (passive_len > m0_vec_count(&buf_bulk->nb_buffer.ov_vec))
		return -E2BIG;

	/* optimizing memory allocation */
	if (buf_bulk->nb_desc.nbd_len != desc_len) {
		/* free old */
		m0_free(buf_bulk->nb_desc.nbd_data);
		buf_bulk->nb_desc.nbd_len = 0;
		/* alloc new */
		buf_bulk->nb_desc.nbd_data = m0_alloc(desc_len);
		if (buf_bulk->nb_desc.nbd_data == NULL)
			return -ENOMEM;
		buf_bulk->nb_desc.nbd_len = desc_len;
	}

	rc_bcount = bufvec_read(&cur_buf, buf_bulk->nb_desc.nbd_data, desc_len);
	if (rc_bcount != desc_len)
		return -EBADMSG;

	buf_ping->nb_length += M0_NET_TEST_STRLEN_NBD_HEADER + desc_len;
	return 0;
}

struct m0_net_buffer *
m0_net_test_network_buf(struct m0_net_test_network_ctx *ctx,
			enum m0_net_test_network_buf_type buf_type,
			uint32_t buf_index)
{
	M0_PRE(ctx != NULL);
	M0_PRE(buf_type == M0_NET_TEST_BUF_PING ||
	       buf_type == M0_NET_TEST_BUF_BULK);
	M0_PRE(buf_index < (buf_type == M0_NET_TEST_BUF_PING ?
	       ctx->ntc_buf_ping_nr : ctx->ntc_buf_bulk_nr));

	return buf_type == M0_NET_TEST_BUF_PING ?
		&ctx->ntc_buf_ping[buf_index] : &ctx->ntc_buf_bulk[buf_index];
}

/** @todo isn't safe because net_test_buf_init() can fail */
int m0_net_test_network_buf_resize(struct m0_net_test_network_ctx *ctx,
				   enum m0_net_test_network_buf_type buf_type,
				   uint32_t buf_index,
				   m0_bcount_t new_size) {
	struct m0_net_buffer *buf;

	M0_PRE(ctx != NULL);

	buf = m0_net_test_network_buf(ctx, buf_type, buf_index);
	M0_ASSERT(buf != NULL);

	net_test_buf_fini(buf, ctx->ntc_dom);
	return net_test_buf_init(buf, new_size, ctx);
}

void m0_net_test_network_buf_fill(struct m0_net_test_network_ctx *ctx,
				  enum m0_net_test_network_buf_type buf_type,
				  uint32_t buf_index,
				  uint8_t fill)
{
	struct m0_bufvec       *bv;
	struct m0_bufvec_cursor bc;
	m0_bcount_t		length;
	m0_bcount_t		i;
	bool			rc_bool;

	M0_PRE(m0_net_test_network_ctx_invariant(ctx));

	bv = &m0_net_test_network_buf(ctx, buf_type, buf_index)->nb_buffer;
	M0_ASSERT(bv != NULL);
	length = m0_vec_count(&bv->ov_vec);
	m0_bufvec_cursor_init(&bc, bv);
	/** @todo use m0_bufvec_cursor_step */
	for (i = 0; i < length; ++i) {
		* (uint8_t *) m0_bufvec_cursor_addr(&bc) = fill;
		m0_bufvec_cursor_move(&bc, 1);
	}
	rc_bool = m0_bufvec_cursor_move(&bc, 0);
	M0_ASSERT(rc_bool);
}

struct m0_net_end_point *
m0_net_test_network_ep(struct m0_net_test_network_ctx *ctx, size_t ep_index)
{
	M0_PRE(m0_net_test_network_ctx_invariant(ctx));
	M0_PRE(ep_index < ctx->ntc_ep_nr);

	return ctx->ntc_ep[ep_index];
}

ssize_t m0_net_test_network_ep_search(struct m0_net_test_network_ctx *ctx,
				      const char *ep_addr)
{
	size_t addr_len = strlen(ep_addr) + 1;
	size_t i;

	for (i = 0; i < ctx->ntc_ep_nr; ++i)
		if (strncmp(ep_addr, ctx->ntc_ep[i]->nep_addr, addr_len) == 0)
			return i;
	return -1;
}

struct m0_net_test_network_timeouts m0_net_test_network_timeouts_never(void)
{
	struct m0_net_test_network_timeouts result;
	int				    i;

	for (i = 0; i < M0_NET_QT_NR; ++i)
		result.ntnt_timeout[i] = M0_TIME_NEVER;
	return result;
}

/**
   @} end of NetTestNetworkInternals group
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
