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
 * Original author: Madhavrao Vemuri <madhav_vemuri@xyratex.com>
 * Original creation date: 19/3/2012
 */

/*
 * This file is depends on bulk_if.c and is included in it, so that bulk
 * interface dummy transport is reused to test auto provisioning of receive
 * message queue.
 */

#include "net/buffer_pool.h"

static int max_recv_msgs = 1;
enum {
	POOL_COLOURS   = 5,
	POOL_THRESHOLD = 2,
	POOL_BUF_NR    = 8,
	MIN_RECV_SIZE  = 1 << 12,
};
static int ut_tm_prov_event_cb_calls = 0;
void ut_tm_prov_event_cb(const struct c2_net_tm_event *ev)
{
	C2_CNT_INC(ut_tm_prov_event_cb_calls);
}

/* UT transfer machine */
static struct c2_net_tm_callbacks ut_tm_prov_cb = {
	.ntc_event_cb = ut_tm_prov_event_cb
};

static void ut_prov_msg_recv_cb(const struct c2_net_buffer_event *ev)
{
	struct c2_net_transfer_mc *tm;
	struct c2_net_buffer	  *nb;
	struct c2_tl		  *ql;
	int			   rc;

	C2_UT_ASSERT(ev != NULL && ev->nbe_buffer != NULL);
	nb = ev->nbe_buffer;
	tm = nb->nb_tm;
	C2_UT_ASSERT(tm->ntm_recv_pool != NULL && nb->nb_pool != NULL);
	ql = &tm->ntm_q[C2_NET_QT_MSG_RECV];

	if (nb->nb_tm->ntm_state == C2_NET_TM_STARTED &&
	  !(nb->nb_flags & C2_NET_BUF_RETAIN)) {
		if (nb->nb_pool->nbp_free > 0)
			C2_UT_ASSERT(c2_atomic64_get(
				&tm->ntm_recv_queue_deficit) == 0);
		rc = c2_net_buffer_add(nb, tm);
		C2_UT_ASSERT(rc == 0);
	} else if (nb->nb_tm->ntm_state == C2_NET_TM_STOPPED ||
		   nb->nb_tm->ntm_state == C2_NET_TM_STOPPING ||
		   nb->nb_tm->ntm_state == C2_NET_TM_FAILED) {
			c2_net_buffer_pool_lock(tm->ntm_recv_pool);
			c2_net_buffer_pool_put(tm->ntm_recv_pool, nb,
					       tm->ntm_pool_colour);
			c2_net_buffer_pool_unlock(tm->ntm_recv_pool);
	}
}

struct c2_net_buffer_callbacks ut_buf_prov_cb = {
	.nbc_cb = {
		[C2_NET_QT_MSG_RECV]          = ut_prov_msg_recv_cb,
		[C2_NET_QT_MSG_SEND]          = ut_msg_send_cb,
		[C2_NET_QT_PASSIVE_BULK_RECV] = ut_passive_bulk_recv_cb,
		[C2_NET_QT_PASSIVE_BULK_SEND] = ut_passive_bulk_send_cb,
		[C2_NET_QT_ACTIVE_BULK_RECV]  = ut_active_bulk_recv_cb,
		[C2_NET_QT_ACTIVE_BULK_SEND]  = ut_active_bulk_send_cb
	},
};

static void ut_pool_low(struct c2_net_buffer_pool *bp)
{
	/* Buffer pool is below threshold.  */
}

static bool pool_not_empty_called = false;
static void ut_pool_not_empty(struct c2_net_buffer_pool *bp)
{
	c2_net_domain_buffer_pool_not_empty(bp);
	pool_not_empty_called = true;
}

static const struct c2_net_buffer_pool_ops b_ops = {
	.nbpo_not_empty	      = ut_pool_not_empty,
	.nbpo_below_threshold = ut_pool_low,
};

static bool ut_tm_prov_stop_called = false;
static int ut_tm_prov_stop(struct c2_net_transfer_mc *tm, bool cancel)
{
	int		      rc;
	struct c2_clink	      tmwait;
	struct c2_net_buffer *nb;
	struct c2_tl	     *ql;

	c2_clink_init(&tmwait, NULL);
	ql = &tm->ntm_q[C2_NET_QT_MSG_RECV];

	C2_UT_ASSERT(c2_mutex_is_locked(&tm->ntm_mutex));
	c2_mutex_unlock(&tm->ntm_mutex);
	c2_tlist_for(&tm_tl, ql, nb) {
		ut_buf_del_called = false;
		c2_clink_add(&tm->ntm_chan, &tmwait);
		c2_net_buffer_del(nb, tm);
		C2_UT_ASSERT(ut_buf_del_called);
		/* wait on channel for post (and consume UT thread) */
		c2_chan_wait(&tmwait);
		c2_clink_del(&tmwait);
		rc = c2_thread_join(&ut_del_thread);
		C2_UT_ASSERT(rc == 0);
	} c2_tlist_endfor;

	ut_tm_prov_stop_called = true;
	c2_mutex_lock(&tm->ntm_mutex);
	rc = ut_tm_stop(tm, false);
	return rc;
}

static struct c2_net_transfer_mc ut_prov_tm1 = {
	.ntm_callbacks = &ut_tm_prov_cb,
	.ntm_state = C2_NET_TM_UNDEFINED
};

static struct c2_net_transfer_mc ut_prov_tm2 = {
	.ntm_callbacks = &ut_tm_prov_cb,
	.ntm_state = C2_NET_TM_UNDEFINED
};

static struct c2_net_domain ut_prov_dom;
#define Debug 0
static void test_net_tm_prov(void)
{
	int rc;
	struct c2_net_transfer_mc *tm1  = &ut_prov_tm1;
	struct c2_net_transfer_mc *tm2 = &ut_prov_tm2;
	struct c2_net_domain	  *dom = &ut_prov_dom;
	struct c2_net_buffer	  *nb;
	c2_bcount_t		   buf_size;
	c2_bcount_t		   buf_seg_size;
	uint32_t		   buf_segs;
	struct c2_clink		   tmwait;
	static uint32_t		   tm_colours = 0;
	struct c2_net_buffer_pool *pool_prov;

	ut_xprt_ops.xo_tm_stop = ut_tm_prov_stop;

	/* initialize the domain */
	ut_dom_init_called = false;
	rc = c2_net_domain_init(dom, &ut_xprt);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_dom_init_called);
	C2_UT_ASSERT(dom->nd_xprt == &ut_xprt);
	C2_UT_ASSERT(dom->nd_xprt_private == &ut_xprt_pvt);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));

	C2_ALLOC_PTR(pool_prov);
	C2_UT_ASSERT(pool_prov != NULL);
	pool_prov->nbp_ops = &b_ops;

	/* get max buffer size */
	buf_size = c2_net_domain_get_max_buffer_size(dom);
	C2_UT_ASSERT(buf_size == UT_MAX_BUF_SIZE);

	/* get max buffer segment size */
	buf_seg_size = c2_net_domain_get_max_buffer_segment_size(dom);
	C2_UT_ASSERT(buf_seg_size == UT_MAX_BUF_SEGMENT_SIZE);

	/* get max buffer segments */
	buf_segs = c2_net_domain_get_max_buffer_segments(dom);
	C2_UT_ASSERT(buf_segs == UT_MAX_BUF_SEGMENTS);

	/* allocate buffers for testing */
	c2_net_buffer_pool_init(pool_prov, dom, POOL_THRESHOLD, buf_segs,
				buf_seg_size, POOL_COLOURS);
	c2_net_buffer_pool_lock(pool_prov);
	rc = c2_net_buffer_pool_provision(pool_prov, POOL_BUF_NR);
	c2_net_buffer_pool_unlock(pool_prov);
	C2_UT_ASSERT(rc == POOL_BUF_NR);

	/* TM init with callbacks */
	rc = c2_net_tm_init(tm1, dom);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_init_called);
	C2_UT_ASSERT(tm1->ntm_state == C2_NET_TM_INITIALIZED);
	C2_UT_ASSERT(c2_list_contains(&dom->nd_tms, &tm1->ntm_dom_linkage));

	/* API Tests */
	c2_net_tm_colour_set(tm1, ++tm_colours);
	C2_UT_ASSERT(tm_colours < POOL_COLOURS);
	C2_UT_ASSERT(c2_net_tm_colour_get(tm1) == tm_colours);

	rc = c2_net_tm_pool_attach(tm1, pool_prov, &ut_buf_prov_cb,
				   MIN_RECV_SIZE, max_recv_msgs);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(tm1->ntm_recv_pool == pool_prov);
	C2_UT_ASSERT(tm1->ntm_recv_pool_callbacks == &ut_buf_prov_cb);
	C2_UT_ASSERT(tm1->ntm_recv_queue_min_recv_size == MIN_RECV_SIZE);
	C2_UT_ASSERT(tm1->ntm_recv_queue_max_recv_msgs == max_recv_msgs);
	C2_UT_ASSERT(tm1->ntm_recv_queue_min_length == C2_NET_TM_RECV_QUEUE_DEF_LEN);

	/* TM start */
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&tm1->ntm_chan, &tmwait);
	ut_end_point_create_called = false;
	C2_UT_ASSERT(tm_tlist_length(&tm1->ntm_q[C2_NET_QT_MSG_RECV]) == 0);
	rc = c2_net_tm_start(tm1, "addr1");
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_start_called);
	C2_UT_ASSERT(tm1->ntm_state == C2_NET_TM_STARTING ||
		     tm1->ntm_state == C2_NET_TM_STARTED);

	/* wait on channel until TM state changed to started */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	C2_UT_ASSERT(ut_tm_prov_event_cb_calls == 1);
	C2_UT_ASSERT(tm1->ntm_state == C2_NET_TM_STARTED);
	C2_UT_ASSERT(ut_end_point_create_called);
	C2_UT_ASSERT(tm1->ntm_ep != NULL);
	C2_UT_ASSERT(c2_atomic64_get(&tm1->ntm_ep->nep_ref.ref_cnt) == 1);
	/*
	 * When TM starts initial provisioning happens before the channel is
	 * notified of the state change.
	 * Check for initial provisioning.
	 */
	C2_UT_ASSERT(tm_tlist_length(&tm1->ntm_q[C2_NET_QT_MSG_RECV]) != 0);
	C2_UT_ASSERT(tm_tlist_length(&tm1->ntm_q[C2_NET_QT_MSG_RECV]) ==
		     tm1->ntm_recv_queue_min_length);
	C2_UT_ASSERT(pool_prov->nbp_free == pool_prov->nbp_buf_nr -
		     tm1->ntm_recv_queue_min_length);
	/* clean up; real xprt would handle this itself */
	c2_thread_join(&ut_tm_thread);
	c2_thread_fini(&ut_tm_thread);

	/*
	 * Check for provisioning when minimum buffers in the receive queue of
	 * tm is changed. Re-provisioning happens synchronously with this call.
	 */
	c2_net_tm_pool_length_set(tm1, 4);
	C2_UT_ASSERT(tm1->ntm_recv_queue_min_length == 4);
	C2_UT_ASSERT(pool_prov->nbp_free == pool_prov->nbp_buf_nr -
		     tm1->ntm_recv_queue_min_length);
	C2_UT_ASSERT(c2_atomic64_get(&tm1->ntm_recv_queue_deficit) == 0);

	/* Check for deficit when required buffers are more than that of pool.*/
	c2_net_tm_pool_length_set(tm1, 10);
	C2_UT_ASSERT(tm1->ntm_recv_queue_min_length == 10);
	C2_UT_ASSERT(pool_prov->nbp_free == 0);
	C2_UT_ASSERT(c2_atomic64_get(&tm1->ntm_recv_queue_deficit) == 2);

	/* Check for provisioning when pool is replenished. */
	pool_not_empty_called = false;
	c2_net_buffer_pool_lock(pool_prov);
	rc = c2_net_buffer_pool_provision(pool_prov, 2);
	c2_net_buffer_pool_unlock(pool_prov);
	C2_UT_ASSERT(rc == 2);
	C2_UT_ASSERT(pool_not_empty_called);
	C2_UT_ASSERT(c2_atomic64_get(&tm1->ntm_recv_queue_deficit) == 0);
	C2_UT_ASSERT(tm1->ntm_recv_queue_min_length == 10);
	C2_UT_ASSERT(pool_prov->nbp_free == pool_prov->nbp_buf_nr -
		     tm1->ntm_recv_queue_min_length);

	/* Initialize another TM iwith different colour. */
	rc = c2_net_tm_init(tm2, dom);
	C2_UT_ASSERT(rc == 0);

	c2_net_tm_colour_set(tm2, ++tm_colours);
	C2_UT_ASSERT(tm_colours < POOL_COLOURS);
	C2_UT_ASSERT(c2_net_tm_colour_get(tm2) == tm_colours);
	max_recv_msgs = 2;
	rc = c2_net_tm_pool_attach(tm2, pool_prov, &ut_buf_prov_cb,
				   MIN_RECV_SIZE, max_recv_msgs);
	C2_UT_ASSERT(tm2->ntm_recv_pool == pool_prov);
	C2_UT_ASSERT(tm2->ntm_recv_pool_callbacks == &ut_buf_prov_cb);
	C2_UT_ASSERT(tm2->ntm_recv_queue_min_recv_size == MIN_RECV_SIZE);
	C2_UT_ASSERT(tm2->ntm_recv_queue_max_recv_msgs == max_recv_msgs);

	/* TM1 start */
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&tm2->ntm_chan, &tmwait);
	ut_end_point_create_called = false;
	C2_UT_ASSERT(tm_tlist_length(&tm2->ntm_q[C2_NET_QT_MSG_RECV]) == 0);
	rc = c2_net_tm_start(tm2, "addr2");
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_start_called);
	C2_UT_ASSERT(tm2->ntm_state == C2_NET_TM_STARTING ||
		     tm2->ntm_state == C2_NET_TM_STARTED);

	/* wait on channel until TM state changed to started */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	C2_UT_ASSERT(tm2->ntm_state == C2_NET_TM_STARTED);
	C2_UT_ASSERT(ut_end_point_create_called);
	/* No buffers to initially provision TM1. */
	C2_UT_ASSERT(tm_tlist_length(&tm2->ntm_q[C2_NET_QT_MSG_RECV]) == 0);
	C2_UT_ASSERT(c2_atomic64_get(&tm2->ntm_recv_queue_deficit) == 2);

	/* clean up; real xprt would handle this itself */
	c2_thread_join(&ut_tm_thread);
	c2_thread_fini(&ut_tm_thread);

	/*
	 * Check for domain wide provisioning when pool is replenished.
	 * As pool is empty make deficit in TM1 to 3.so that both the TM's
	 * are provisioned.
	 */
	c2_net_tm_pool_length_set(tm1, 13);
	C2_UT_ASSERT(tm1->ntm_recv_queue_min_length == 13);
	C2_UT_ASSERT(pool_prov->nbp_free == 0);
	C2_UT_ASSERT(c2_atomic64_get(&tm1->ntm_recv_queue_deficit) == 3);

	pool_not_empty_called = false;
	c2_net_buffer_pool_lock(pool_prov);
	rc = c2_net_buffer_pool_provision(pool_prov, 6);
	c2_net_buffer_pool_unlock(pool_prov);
	C2_UT_ASSERT(rc == 6);
	C2_UT_ASSERT(pool_not_empty_called);
	C2_UT_ASSERT(c2_atomic64_get(&tm1->ntm_recv_queue_deficit) == 0);
	C2_UT_ASSERT(c2_atomic64_get(&tm2->ntm_recv_queue_deficit) == 0);
	C2_UT_ASSERT(tm_tlist_length(&tm2->ntm_q[C2_NET_QT_MSG_RECV]) ==
		     tm2->ntm_recv_queue_min_length);
	C2_UT_ASSERT(pool_prov->nbp_free == pool_prov->nbp_buf_nr -
		     tm1->ntm_recv_queue_min_length -
		     tm2->ntm_recv_queue_min_length);

	/*
	 * Check that when a buffer is dequeued from receive message queue,
	 * re-provision of the queue happens before the callback is called in
	 * which either buffers are re-provisioned or returned to the pool.
	 */
	ut_buf_del_called = false;
	C2_UT_ASSERT(pool_prov->nbp_free == 1);
	nb = tm_tlist_head(&tm1->ntm_q[C2_NET_QT_MSG_RECV]);
	c2_clink_add(&tm1->ntm_chan, &tmwait);
	c2_net_buffer_del(nb, tm1);
	C2_UT_ASSERT(ut_buf_del_called);
	/* wait on channel for post (and consume UT thread) */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	rc = c2_thread_join(&ut_del_thread);
	C2_UT_ASSERT(rc == 0);
	/* Pool is empty. */
	C2_UT_ASSERT(pool_prov->nbp_free == 0);
	C2_UT_ASSERT(tm_tlist_length(&tm1->ntm_q[C2_NET_QT_MSG_RECV]) ==
		     tm1->ntm_recv_queue_min_length + 1);

	/*
	 * When TM stop is called it returns buffers in TM receive queue to the pool
	 * in ut_prov_msg_recv_cb. As pool is empty, adding buffers to it will trigger
	 * c2_net_buffer_pool_not_empty cb which does the domain wide re-provisioning
	 * based on deficit value.
	 *
	 * To test use case "return a buffer to the pool and trigger re-provisioning",
	 * do the following.
	 * - Create some deficit in TM2.
	 * - Stop the TM1
	 *   As a result of this buffers used in TM1 will be returned to the empty pool
	 *   and will be used to provision TM2.
	 *
	 * It also tests use case "buffer colour correctness during auto provisioning".
	 * - As buffers in TM1 having colour1 will be returned to the empty pool
	 *   and are later provisioned to TM2.
	 */
	pool_not_empty_called = false;
	/* Check buffer pool is empty. */
	C2_UT_ASSERT(pool_prov->nbp_free == 0);
	/* Create deficit of 10 buffers in TM2. */
	c2_net_tm_pool_length_set(tm2, 12);
	C2_UT_ASSERT(c2_atomic64_get(&tm2->ntm_recv_queue_deficit) == 10);

	/* TM stop and fini */
	c2_clink_add(&tm1->ntm_chan, &tmwait);
	rc = c2_net_tm_stop(tm1, false);
	C2_UT_ASSERT(rc == 0);
	c2_chan_wait(&tmwait);
	/* Check whether all buffers are returned to the pool. */
	C2_UT_ASSERT(pool_prov->nbp_free == pool_prov->nbp_buf_nr -
		     tm2->ntm_recv_queue_min_length);
	c2_clink_del(&tmwait);
	c2_thread_join(&ut_tm_thread); /* cleanup thread */
	c2_thread_fini(&ut_tm_thread);
	c2_net_tm_fini(tm1);
	C2_UT_ASSERT(pool_not_empty_called);
	C2_UT_ASSERT(pool_prov->nbp_free == 4);
	/* TM2 is provisioned with buffers of TM1 returned to the pool. */
	C2_UT_ASSERT(c2_atomic64_get(&tm2->ntm_recv_queue_deficit) == 0);

	/* TM2 stop and fini */
	c2_clink_add(&tm2->ntm_chan, &tmwait);
	rc = c2_net_tm_stop(tm2, false);
	C2_UT_ASSERT(rc == 0);
	c2_chan_wait(&tmwait);
	/* Check whether all buffers are returned to the pool. */
	C2_UT_ASSERT(pool_prov->nbp_free ==
		     pool_prov->nbp_buf_nr);
	c2_clink_del(&tmwait);
	c2_thread_join(&ut_tm_thread); /* cleanup thread */
	c2_thread_fini(&ut_tm_thread);
	c2_net_tm_fini(tm2);

	c2_clink_fini(&tmwait);
	/* Finalize the buffer pool. */
	c2_net_buffer_pool_lock(pool_prov);
	c2_net_buffer_pool_fini(pool_prov);
	c2_free(pool_prov);

	/* fini the domain */
	ut_dom_fini_called = false;
	c2_net_domain_fini(dom);
	C2_UT_ASSERT(ut_dom_fini_called);
	ut_dom_init_called = false;
}

const struct c2_test_suite c2_net_tm_prov_ut = {
        .ts_name = "net-prov-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "net_prov", test_net_tm_prov},
                { NULL, NULL }
        }
};
C2_EXPORTED(c2_net_tm_prov_ut);
