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
 * Original creation date: 05/19/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* @todo remove */
#ifndef __KERNEL__
#include <stdio.h>		/* printf */
#endif

#include "lib/ut.h"		/* C2_UT_ASSERT */
#include "lib/semaphore.h"	/* c2_semaphore */
#include "lib/memory.h"		/* c2_alloc */
#include "net/lnet/lnet.h"	/* c2_net_lnet_ifaces_get */

#include "net/test/network.h"

/* @todo debug only, remove it */
#ifndef __KERNEL__
#define LOGD(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOGD(format, ...) do {} while (0)
#endif

enum {
	NET_TEST_PING_BUF_SIZE = 4096,
	NET_TEST_PING_BUF_STEP = 511,	/** @see c2_net_test_network_ut_ping */
	NET_TEST_BULK_BUF_SIZE = 1024 * 1024,
	NET_TEST_BUF_DESC_NR   = 10,
};

static c2_bcount_t bv_copy(struct c2_bufvec *dst,
			   struct c2_bufvec *src,
			   c2_bcount_t len)
{
	struct c2_bufvec_cursor bcsrc;
	struct c2_bufvec_cursor bcdst;

	c2_bufvec_cursor_init(&bcsrc, src);
	c2_bufvec_cursor_init(&bcdst, dst);
	return c2_bufvec_cursor_copy(&bcdst, &bcsrc, len);
}

/* @todo too expensive, use c2_bufvec_cursor_step() + memcmp() */
static bool net_buf_data_eq(enum c2_net_test_network_buf_type buf_type,
			    struct c2_net_test_network_ctx *ctx1,
			    uint32_t buf_index1,
			    struct c2_net_test_network_ctx *ctx2,
			    uint32_t buf_index2)
{
	void		     *b1_data;
	void		     *b2_data;
	c2_bcount_t	      length;
	struct c2_net_buffer *b1;
	struct c2_net_buffer *b2;
	struct c2_bufvec      bv1 = C2_BUFVEC_INIT_BUF(&b1_data, &length);
	struct c2_bufvec      bv2 = C2_BUFVEC_INIT_BUF(&b2_data, &length);
	c2_bcount_t	      rc_bcount;
	bool		      rc;

	b1 = c2_net_test_network_buf(ctx1, buf_type, buf_index1);
	b2 = c2_net_test_network_buf(ctx2, buf_type, buf_index2);

	if (b1->nb_length != b2->nb_length)
		return false;

	length = b1->nb_length;

	b1_data = c2_alloc(length);
	b2_data = c2_alloc(length);

	rc_bcount = bv_copy(&bv1, &b1->nb_buffer, length);
	C2_ASSERT(rc_bcount == length);
	rc_bcount = bv_copy(&bv2, &b2->nb_buffer, length);
	C2_ASSERT(rc_bcount == length);

	rc = memcmp(b1_data, b2_data, length) == 0;

	c2_free(b1_data);
	c2_free(b2_data);

	return rc;
}

static void ping_tm_event_cb(const struct c2_net_tm_event *ev)
{
}

static struct c2_semaphore recv_sem;
static struct c2_semaphore send_sem;

static void ping_cb_msg_recv(struct c2_net_test_network_ctx *ctx,
			     const uint32_t buf_index,
			     enum c2_net_queue_type q,
			     const struct c2_net_buffer_event *ev)
{
	c2_semaphore_up(&recv_sem);
}

static void ping_cb_msg_send(struct c2_net_test_network_ctx *ctx,
			     const uint32_t buf_index,
			     enum c2_net_queue_type q,
			     const struct c2_net_buffer_event *ev)
{
	c2_semaphore_up(&send_sem);
}

static void ping_cb_impossible(struct c2_net_test_network_ctx *ctx,
			       const uint32_t buf_index,
			       enum c2_net_queue_type q,
			       const struct c2_net_buffer_event *ev)
{

	C2_IMPOSSIBLE("impossible bulk buffer callback in ping test");
}

static const struct c2_net_tm_callbacks ping_tm_cb = {
	.ntc_event_cb = ping_tm_event_cb
};

static struct c2_net_test_network_buffer_callbacks ping_buf_cb = {
	.ntnbc_cb = {
		[C2_NET_QT_MSG_RECV]		= ping_cb_msg_recv,
		[C2_NET_QT_MSG_SEND]		= ping_cb_msg_send,
		[C2_NET_QT_PASSIVE_BULK_RECV]	= ping_cb_impossible,
		[C2_NET_QT_PASSIVE_BULK_SEND]	= ping_cb_impossible,
		[C2_NET_QT_ACTIVE_BULK_RECV]	= ping_cb_impossible,
		[C2_NET_QT_ACTIVE_BULK_SEND]	= ping_cb_impossible,
	}
};

void c2_net_test_network_ut_ping(void)
{
	static struct c2_net_test_network_ctx send;
	static struct c2_net_test_network_ctx recv;
	int				      rc;
	bool				      rc_bool;
	c2_bcount_t			      buf_size;

	buf_size = NET_TEST_PING_BUF_SIZE;
	rc = c2_net_test_network_ctx_init(&send, "0@lo:12345:30:4000",
					  &ping_tm_cb, &ping_buf_cb,
					  buf_size, 1,
					  0, 0,
					  1, NULL);
	C2_UT_ASSERT(rc == 0);

	rc = c2_net_test_network_ctx_init(&recv, "0@lo:12345:30:4001",
					  &ping_tm_cb, &ping_buf_cb,
					  buf_size, 1,
					  0, 0,
					  1, NULL);
	C2_UT_ASSERT(rc == 0);

	rc = c2_net_test_network_ep_add(&send, "0@lo:12345:30:4001");
	C2_UT_ASSERT(rc == 0);
	rc = c2_net_test_network_ep_add(&recv, "0@lo:12345:30:4000");
	C2_UT_ASSERT(rc == 0);

	c2_semaphore_init(&recv_sem, 0);
	c2_semaphore_init(&send_sem, 0);

	while (buf_size > 0) {
		/* test buffer resize. @see c2_net_test_network_buf_resize */
		c2_net_test_network_buf_resize(&send, C2_NET_TEST_BUF_PING, 0,
					       buf_size);
		c2_net_test_network_buf_resize(&recv, C2_NET_TEST_BUF_PING, 0,
					       buf_size);

		c2_net_test_network_buf_fill(&send, C2_NET_TEST_BUF_PING, 0, 1);
		c2_net_test_network_buf_fill(&recv, C2_NET_TEST_BUF_PING, 0, 2);
		rc_bool = net_buf_data_eq(C2_NET_TEST_BUF_PING,
					  &send, 0, &recv, 0);
		C2_ASSERT(!rc_bool);

		rc = c2_net_test_network_msg_recv(&recv, 0);
		C2_UT_ASSERT(rc == 0);
		rc = c2_net_test_network_msg_send(&send, 0, 0);
		C2_UT_ASSERT(rc == 0);

		/* @todo timeddown */
		c2_semaphore_down(&recv_sem);
		c2_semaphore_down(&send_sem);

		rc_bool = net_buf_data_eq(C2_NET_TEST_BUF_PING,
					  &send, 0, &recv, 0);
		C2_ASSERT(rc_bool);

		buf_size = buf_size < NET_TEST_PING_BUF_STEP ?
			   0 : buf_size - NET_TEST_PING_BUF_STEP;
	}

	c2_semaphore_fini(&recv_sem);
	c2_semaphore_fini(&send_sem);

	c2_net_test_network_ctx_fini(&send);
	c2_net_test_network_ctx_fini(&recv);
}

static struct c2_semaphore bulk_cb_sem[C2_NET_QT_NR];
static bool bulk_offset_mismatch;

static void bulk_cb(struct c2_net_test_network_ctx *ctx,
		    const uint32_t buf_index,
		    enum c2_net_queue_type q,
		    const struct c2_net_buffer_event *ev)
{
	C2_PRE(q < C2_NET_QT_NR);

	/*
	   c2_net_buffer_event.nbe_offset can't have non-zero value
	   in this test.
	 */
	if (ev->nbe_offset != 0)
		bulk_offset_mismatch = true;

	c2_semaphore_up(&bulk_cb_sem[q]);
}

static const struct c2_net_tm_callbacks bulk_tm_cb = {
	.ntc_event_cb = ping_tm_event_cb
};

static struct c2_net_test_network_buffer_callbacks bulk_buf_cb = {
	.ntnbc_cb = {
		[C2_NET_QT_MSG_RECV]		= bulk_cb,
		[C2_NET_QT_MSG_SEND]		= bulk_cb,
		[C2_NET_QT_PASSIVE_BULK_RECV]	= bulk_cb,
		[C2_NET_QT_PASSIVE_BULK_SEND]	= bulk_cb,
		[C2_NET_QT_ACTIVE_BULK_RECV]	= bulk_cb,
		[C2_NET_QT_ACTIVE_BULK_SEND]	= bulk_cb,
	}
};

void c2_net_test_network_ut_bulk(void)
{
	static struct c2_net_test_network_ctx client;
	static struct c2_net_test_network_ctx server;
	int				      rc;
	int				      rc_u32;
	int				      i;
	bool				      rc_bool;

	rc = c2_net_test_network_ctx_init(&client, "0@lo:12345:30:4000",
					  &bulk_tm_cb, &bulk_buf_cb,
					  NET_TEST_PING_BUF_SIZE, 1,
					  NET_TEST_BULK_BUF_SIZE, 2,
					  1, NULL);
	C2_UT_ASSERT(rc == 0);
	rc = c2_net_test_network_ctx_init(&server, "0@lo:12345:30:4001",
					  &bulk_tm_cb, &bulk_buf_cb,
					  NET_TEST_PING_BUF_SIZE, 1,
					  NET_TEST_BULK_BUF_SIZE, 1,
					  1, NULL);
	C2_UT_ASSERT(rc == 0);

	rc = c2_net_test_network_ep_add(&client, "0@lo:12345:30:4001");
	C2_UT_ASSERT(rc == 0);
	rc = c2_net_test_network_ep_add(&server, "0@lo:12345:30:4000");
	C2_UT_ASSERT(rc == 0);

	/* start of bulk send/recv */
	bulk_offset_mismatch = false;
	/* fill bulk buffers with different values */
	c2_net_test_network_buf_fill(&client, C2_NET_TEST_BUF_BULK, 0, 10);
	c2_net_test_network_buf_fill(&client, C2_NET_TEST_BUF_BULK, 1, 20);
	c2_net_test_network_buf_fill(&server, C2_NET_TEST_BUF_BULK, 0, 30);
	rc_bool = net_buf_data_eq(C2_NET_TEST_BUF_BULK, &client, 0, &server, 0);
	C2_ASSERT(!rc_bool);
	rc_bool = net_buf_data_eq(C2_NET_TEST_BUF_BULK, &client, 1, &server, 0);
	C2_ASSERT(!rc_bool);
	rc_bool = net_buf_data_eq(C2_NET_TEST_BUF_BULK, &client, 0, &client, 1);
	C2_ASSERT(!rc_bool);
	/* init callback semaphores */
	for (i = 0; i < C2_NET_QT_NR; ++i)
		c2_semaphore_init(&bulk_cb_sem[i], 0);
	/* server: receive ping buf */
	rc = c2_net_test_network_msg_recv(&server, 0);
	C2_UT_ASSERT(rc == 0);
	/* client: add passive sender->active receiver bulk buffer to q */
	rc = c2_net_test_network_bulk_enqueue(&client, 0, 0,
			C2_NET_QT_PASSIVE_BULK_SEND);
	C2_UT_ASSERT(rc == 0);
	/* client: add passive receiver<-active sender bulk buffer to q */
	rc = c2_net_test_network_bulk_enqueue(&client, 1, 0,
			C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(rc == 0);
	/* client: add buffer descriptors to ping buf */
	c2_net_test_network_bd_reset(&client, 0);
	rc = c2_net_test_network_bd_encode(&client, 0, 0);
	C2_UT_ASSERT(rc == 0);
	rc = c2_net_test_network_bd_encode(&client, 0, 1);
	C2_UT_ASSERT(rc == 0);
	rc_u32 = c2_net_test_network_bd_count(&client, 0);
	C2_UT_ASSERT(rc_u32 == 2);
	/* client: send ping buf */
	rc = c2_net_test_network_msg_send(&client, 0, 0);
	C2_UT_ASSERT(rc == 0);
	/* server: wait for buf from client */
	c2_semaphore_down(&bulk_cb_sem[C2_NET_QT_MSG_RECV]);
	/* server: check ping buffer size and data */
	rc_bool = net_buf_data_eq(C2_NET_TEST_BUF_PING, &client, 0, &server, 0);
	C2_ASSERT(rc_bool);
	/* server: extract buf descriptor for active recv */
	rc_u32 = c2_net_test_network_bd_count(&server, 0);
	C2_UT_ASSERT(rc_u32 == 2);
	c2_net_test_network_bd_reset(&server, 0);
	rc = c2_net_test_network_bd_decode(&server, 0, 0);
	C2_UT_ASSERT(rc == 0);
	/* server: do active recv */
	rc = c2_net_test_network_bulk_enqueue(&server, 0, 0,
			C2_NET_QT_ACTIVE_BULK_RECV);
	C2_UT_ASSERT(rc == 0);
	/* server: wait for active recv callback */
	c2_semaphore_down(&bulk_cb_sem[C2_NET_QT_ACTIVE_BULK_RECV]);
	/* server: extract buf descriptor for active send */
	rc = c2_net_test_network_bd_decode(&server, 0, 0);
	C2_UT_ASSERT(rc == 0);
	/* server: do active send */
	rc = c2_net_test_network_bulk_enqueue(&server, 0, 0,
			C2_NET_QT_ACTIVE_BULK_SEND);
	C2_UT_ASSERT(rc == 0);
	/* server: wait for active send callbacks */
	c2_semaphore_down(&bulk_cb_sem[C2_NET_QT_ACTIVE_BULK_SEND]);
	/*
	   client: now all data are actually sent, so check for passive
	   send/recv callbacks called
	 */
	/* send message */
	c2_semaphore_down(&bulk_cb_sem[C2_NET_QT_MSG_SEND]);
	/* passive bulk send */
	c2_semaphore_down(&bulk_cb_sem[C2_NET_QT_PASSIVE_BULK_SEND]);
	/* passive bulk recv */
	c2_semaphore_down(&bulk_cb_sem[C2_NET_QT_PASSIVE_BULK_RECV]);
	/* fini callback semaphores */
	for (i = 0; i < C2_NET_QT_NR; ++i)
		c2_semaphore_fini(&bulk_cb_sem[i]);
	/* check for equal bulk buffers on client and server */
	rc_bool = net_buf_data_eq(C2_NET_TEST_BUF_BULK, &client, 0, &server, 0);
	C2_ASSERT(rc_bool);
	rc_bool = net_buf_data_eq(C2_NET_TEST_BUF_BULK, &client, 0, &client, 1);
	C2_ASSERT(rc_bool);
	/* end of bulk send/recv */
	C2_ASSERT(!bulk_offset_mismatch);

	c2_net_test_network_ctx_fini(&client);
	c2_net_test_network_ctx_fini(&server);
}

static void tm_event_cb_empty(const struct c2_net_tm_event *ev)
{
}

static void cb_empty(struct c2_net_test_network_ctx *ctx,
		     const uint32_t buf_index,
		     enum c2_net_queue_type q,
		     const struct c2_net_buffer_event *ev)
{
}

static const struct c2_net_tm_callbacks tm_cb_empty = {
	.ntc_event_cb = tm_event_cb_empty
};

static struct c2_net_test_network_buffer_callbacks buf_cb_empty = {
	.ntnbc_cb = {
		[C2_NET_QT_MSG_RECV]		= cb_empty,
		[C2_NET_QT_MSG_SEND]		= cb_empty,
		[C2_NET_QT_PASSIVE_BULK_RECV]	= cb_empty,
		[C2_NET_QT_PASSIVE_BULK_SEND]	= cb_empty,
		[C2_NET_QT_ACTIVE_BULK_RECV]	= cb_empty,
		[C2_NET_QT_ACTIVE_BULK_SEND]	= cb_empty,
	}
};

/**
   Compare bulk network buffer descriptors.
 */
static bool buf_desc_eq(struct c2_net_test_network_ctx *ctx1,
			uint32_t buf_index1,
			struct c2_net_test_network_ctx *ctx2,
			uint32_t buf_index2)
{
	struct c2_net_buffer   *b1;
	struct c2_net_buffer   *b2;
	struct c2_net_buf_desc *d1;
	struct c2_net_buf_desc *d2;

	b1 = c2_net_test_network_buf(ctx1, C2_NET_TEST_BUF_BULK, buf_index1);
	b2 = c2_net_test_network_buf(ctx2, C2_NET_TEST_BUF_BULK, buf_index2);
	d1 = &b1->nb_desc;
	d2 = &b2->nb_desc;

	return d1->nbd_len == d2->nbd_len &&
		memcmp(d1->nbd_data, d2->nbd_data, d1->nbd_len) == 0;
}

static void multiple_buf_desc_encode_decode(struct c2_net_test_network_ctx *ctx,
					    int count)
{
	int i;
	bool rc_bool;
	uint32_t rc_u32;
	int rc;

	c2_net_test_network_bd_reset(ctx, 0);
	rc_u32 = c2_net_test_network_bd_count(ctx, 0);
	C2_UT_ASSERT(rc_u32 == 0);
	for (i = 0; i < count; ++i) {
		/* encode */
		rc = c2_net_test_network_bd_encode(ctx, 0, i % 2);
		C2_UT_ASSERT(rc == 0);
		/* check number of buf descriptors in the ping buffer */
		rc_u32 = c2_net_test_network_bd_count(ctx, 0);
		C2_UT_ASSERT(rc_u32 == i + 1);
	}
	/* prepare to decode */
	c2_net_test_network_bd_reset(ctx, 0);
	rc_u32 = c2_net_test_network_bd_count(ctx, 0);
	C2_UT_ASSERT(rc_u32 == 0);
	for (i = 0; i < count; ++i) {
		/* decode */
		rc = c2_net_test_network_bd_decode(ctx, 0, 2 + i % 2);
		C2_UT_ASSERT(rc == 0);
		/* check number of buf descriptors in the ping buffer */
		rc_u32 = c2_net_test_network_bd_count(ctx, 0);
		C2_UT_ASSERT(rc_u32 == i + 1);
		/* compare c2_net_buf_desc's */
		rc_bool = buf_desc_eq(ctx, i % 2, ctx, 2 + i % 2);
		C2_UT_ASSERT(rc_bool);
	}
}

void c2_net_test_network_ut_buf_desc(void)
{
	static struct c2_net_test_network_ctx ctx;
	int i;
	int rc;
	static struct c2_clink tmwait;

	rc = c2_net_test_network_ctx_init(&ctx, "0@lo:12345:30:*",
					  &tm_cb_empty, &buf_cb_empty,
					  NET_TEST_PING_BUF_SIZE, 2,
					  NET_TEST_BULK_BUF_SIZE, 4,
					  1, NULL);
	C2_UT_ASSERT(rc == 0);

	/* add some ep - tranfer machine ep */
	rc = c2_net_test_network_ep_add(&ctx, ctx.ntc_tm.ntm_ep->nep_addr);
	C2_UT_ASSERT(rc == 0);

	/* obtain some c2_net_buf_desc */
	rc = c2_net_test_network_bulk_enqueue(&ctx, 0, 0,
					      C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(rc == 0);
	rc = c2_net_test_network_bulk_enqueue(&ctx, 1, 0,
					      C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(rc == 0);

	/* run multiple tests */
	for (i = 0; i < NET_TEST_BUF_DESC_NR; ++i)
		multiple_buf_desc_encode_decode(&ctx, i);

	/* remove bulk buffer from queue */
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&ctx.ntc_tm.ntm_chan, &tmwait);
	c2_net_test_network_buffer_dequeue(&ctx, C2_NET_TEST_BUF_BULK, 0);
	c2_chan_wait(&tmwait);
	c2_net_test_network_buffer_dequeue(&ctx, C2_NET_TEST_BUF_BULK, 1);
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	c2_clink_fini(&tmwait);

	c2_net_test_network_ctx_fini(&ctx);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
