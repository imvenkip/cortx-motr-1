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

int c2_ut_xprt_buffer_min_recv_size(struct c2_net_domain *ndom)
{
	return MIN_RECV_SIZE;
}

int c2_ut_xprt_buffer_max_recv_msgs(struct c2_net_domain *ndom)
{
	return max_recv_msgs;
}

static void low(struct c2_net_buffer_pool *bp)
{
	/* Buffer pool is below threshold.  */
}

static bool pool_not_empty_called = false;
static void not_empty(struct c2_net_buffer_pool *bp)
{
	c2_net_domain_buffer_pool_not_empty(bp);
	pool_not_empty_called = true;
}

static const struct c2_net_buffer_pool_ops b_ops = {
	.nbpo_not_empty	      = not_empty,
	.nbpo_below_threshold = low,
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
	struct c2_net_transfer_mc *tm  = &ut_prov_tm1;
	struct c2_net_transfer_mc *tm1 = &ut_prov_tm2;
	struct c2_net_domain	  *dom = &ut_prov_dom;
	c2_bcount_t		   buf_size;
	c2_bcount_t		   buf_seg_size;
	uint32_t		   buf_segs;
	uint32_t		   pool_colours;
	uint32_t		   pool_threshold;
	uint32_t		   buf_nr;
	struct c2_clink		   tmwait;
	static uint32_t		   tm_colours = 0;

	ut_xprt_ops.xo_tm_stop = ut_tm_prov_stop;
	pool_colours	       = POOL_COLOURS;
	pool_threshold	       = POOL_THRESHOLD;
	buf_nr		       = POOL_BUF_NR;

	/* initialize the domain */
	ut_dom_init_called = false;
	rc = c2_net_domain_init(dom, &ut_xprt);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_dom_init_called);
	C2_UT_ASSERT(dom->nd_xprt == &ut_xprt);
	C2_UT_ASSERT(dom->nd_xprt_private == &ut_xprt_pvt);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));

	C2_ALLOC_PTR(dom->nd_pool);
	C2_UT_ASSERT(dom->nd_pool != NULL);
	dom->nd_pool->nbp_ops = &b_ops;

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
	c2_net_buffer_pool_init(dom->nd_pool, dom, pool_threshold, buf_segs,
				buf_seg_size, pool_colours);
	c2_net_buffer_pool_lock(dom->nd_pool);
	rc = c2_net_buffer_pool_provision(dom->nd_pool, buf_nr);
	c2_net_buffer_pool_unlock(dom->nd_pool);
	C2_UT_ASSERT(rc == buf_nr);

	/* TM init with callbacks */
	rc = c2_net_tm_init(tm, dom);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_init_called);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_INITIALIZED);
	C2_UT_ASSERT(c2_list_contains(&dom->nd_tms, &tm->ntm_dom_linkage));

	/* API Tests */
	c2_net_tm_colour_set(tm, ++tm_colours);
	C2_UT_ASSERT(c2_net_tm_colour_get(tm) == tm_colours);

	rc = c2_net_tm_pool_attach(tm, dom->nd_pool, &ut_buf_prov_cb,
				   c2_ut_xprt_buffer_min_recv_size(dom),
				   c2_ut_xprt_buffer_max_recv_msgs(dom));
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(tm->ntm_recv_pool == dom->nd_pool);
	C2_UT_ASSERT(tm->ntm_recv_pool_callbacks == &ut_buf_prov_cb);
	C2_UT_ASSERT(tm->ntm_recv_queue_min_recv_size == MIN_RECV_SIZE);
	C2_UT_ASSERT(tm->ntm_recv_queue_max_recv_msgs == max_recv_msgs);

	/* TM start */
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&tm->ntm_chan, &tmwait);
	ut_end_point_create_called = false;
	C2_UT_ASSERT(tm_tlist_length(&tm->ntm_q[C2_NET_QT_MSG_RECV]) == 0);
	rc = c2_net_tm_start(tm, "addr1");
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_start_called);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STARTING ||
		     tm->ntm_state == C2_NET_TM_STARTED);

	/* wait on channel until TM state changed to started */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	C2_UT_ASSERT(ut_tm_prov_event_cb_calls == 1);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STARTED);
	C2_UT_ASSERT(ut_end_point_create_called);
	C2_UT_ASSERT(tm->ntm_ep != NULL);
	C2_UT_ASSERT(c2_atomic64_get(&tm->ntm_ep->nep_ref.ref_cnt) == 1);
	/*
	 * When TM starts initial provisioning happens.
	 * Check for initial provisioning.
	 */
	C2_UT_ASSERT(tm_tlist_length(&tm->ntm_q[C2_NET_QT_MSG_RECV]) != 0);
	C2_UT_ASSERT(tm_tlist_length(&tm->ntm_q[C2_NET_QT_MSG_RECV]) ==
		     tm->ntm_recv_queue_min_length);
	C2_UT_ASSERT(tm->ntm_recv_pool->nbp_free ==
		     tm->ntm_recv_pool->nbp_buf_nr -
		     tm->ntm_recv_queue_min_length);
	/* clean up; real xprt would handle this itself */
	c2_thread_join(&ut_tm_thread);
	c2_thread_fini(&ut_tm_thread);

	/* Check for provisioning when length of the tm is changed. */
	c2_net_tm_pool_length_set(tm, 4);
	C2_UT_ASSERT(tm->ntm_recv_queue_min_length == 4);
	C2_UT_ASSERT(tm->ntm_recv_pool->nbp_free ==
		     tm->ntm_recv_pool->nbp_buf_nr -
		     tm->ntm_recv_queue_min_length);

	/* Check for deficit when required buffers are more than that of pool. */
	c2_net_tm_pool_length_set(tm, 10);
	C2_UT_ASSERT(tm->ntm_recv_queue_min_length == 10);
	C2_UT_ASSERT(tm->ntm_recv_pool->nbp_free == 0);
	C2_UT_ASSERT(c2_atomic64_get(&tm->ntm_recv_queue_deficit) == 2);

	/* Check for provisioning when pool is replenished. */
	pool_not_empty_called = false;
	c2_net_buffer_pool_lock(dom->nd_pool);
	rc = c2_net_buffer_pool_provision(dom->nd_pool, 2);
	c2_net_buffer_pool_unlock(dom->nd_pool);
	C2_UT_ASSERT(rc == 2);
	C2_UT_ASSERT(pool_not_empty_called);
	C2_UT_ASSERT(c2_atomic64_get(&tm->ntm_recv_queue_deficit) == 0);
	C2_UT_ASSERT(tm->ntm_recv_queue_min_length == 10);
	C2_UT_ASSERT(tm->ntm_recv_pool->nbp_free ==
		     tm->ntm_recv_pool->nbp_buf_nr -
		     tm->ntm_recv_queue_min_length);

	/* Check provsioning API when there is no deficit. */
	C2_UT_ASSERT(c2_mutex_is_not_locked(&tm->ntm_mutex));
	C2_UT_ASSERT(c2_net_buffer_pool_is_not_locked(tm->ntm_recv_pool));
	C2_CNT_INC(tm->ntm_callback_counter);
	c2_net__tm_provision_recv_q(tm);
	C2_CNT_DEC(tm->ntm_callback_counter);

	rc = c2_net_tm_init(tm1, dom);
	C2_UT_ASSERT(rc == 0);

	c2_net_tm_colour_set(tm1, ++tm_colours);
	C2_UT_ASSERT(c2_net_tm_colour_get(tm1) == tm_colours);
	max_recv_msgs = 2;
	rc = c2_net_tm_pool_attach(tm1, dom->nd_pool, &ut_buf_prov_cb,
				   c2_ut_xprt_buffer_min_recv_size(dom),
				   c2_ut_xprt_buffer_max_recv_msgs(dom));
	C2_UT_ASSERT(tm1->ntm_recv_pool == dom->nd_pool);
	C2_UT_ASSERT(tm1->ntm_recv_pool_callbacks == &ut_buf_prov_cb);
	C2_UT_ASSERT(tm1->ntm_recv_queue_min_recv_size == MIN_RECV_SIZE);
	C2_UT_ASSERT(tm1->ntm_recv_queue_max_recv_msgs == max_recv_msgs);

	/* TM1 start */
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&tm1->ntm_chan, &tmwait);
	ut_end_point_create_called = false;
	C2_UT_ASSERT(tm_tlist_length(&tm1->ntm_q[C2_NET_QT_MSG_RECV]) == 0);
	rc = c2_net_tm_start(tm1, "addr2");
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_start_called);
	C2_UT_ASSERT(tm1->ntm_state == C2_NET_TM_STARTING ||
		     tm1->ntm_state == C2_NET_TM_STARTED);

	/* wait on channel until TM state changed to started */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	C2_UT_ASSERT(tm1->ntm_state == C2_NET_TM_STARTED);
	C2_UT_ASSERT(ut_end_point_create_called);
	/* No buffers to initially provision TM1. */
	C2_UT_ASSERT(tm_tlist_length(&tm1->ntm_q[C2_NET_QT_MSG_RECV]) == 0);
	C2_UT_ASSERT(c2_atomic64_get(&tm1->ntm_recv_queue_deficit) == 2);

	/* clean up; real xprt would handle this itself */
	c2_thread_join(&ut_tm_thread);
	c2_thread_fini(&ut_tm_thread);

	/* Check for domain wide provisioning when pool is replenished. */
	pool_not_empty_called = false;
	c2_net_buffer_pool_lock(dom->nd_pool);
	rc = c2_net_buffer_pool_provision(dom->nd_pool, 3);
	c2_net_buffer_pool_unlock(dom->nd_pool);
	C2_UT_ASSERT(rc == 3);
	C2_UT_ASSERT(pool_not_empty_called);
	C2_UT_ASSERT(c2_atomic64_get(&tm->ntm_recv_queue_deficit) == 0);
	C2_UT_ASSERT(c2_atomic64_get(&tm1->ntm_recv_queue_deficit) == 0);
	C2_UT_ASSERT(tm_tlist_length(&tm1->ntm_q[C2_NET_QT_MSG_RECV]) ==
		     tm1->ntm_recv_queue_min_length);
	C2_UT_ASSERT(tm1->ntm_recv_pool->nbp_free ==
		     tm1->ntm_recv_pool->nbp_buf_nr -
		     tm->ntm_recv_queue_min_length -
		     tm1->ntm_recv_queue_min_length);

	/* TM stop and fini */
	c2_clink_add(&tm->ntm_chan, &tmwait);
	rc = c2_net_tm_stop(tm, false);
	C2_UT_ASSERT(rc == 0);
	c2_chan_wait(&tmwait);
	/* Check whether all buffers are returned to the pool. */
	C2_UT_ASSERT(tm->ntm_recv_pool->nbp_free ==
		     tm->ntm_recv_pool->nbp_buf_nr -
		     tm1->ntm_recv_queue_min_length);
	c2_clink_del(&tmwait);
	c2_thread_join(&ut_tm_thread); /* cleanup thread */
	c2_thread_fini(&ut_tm_thread);
	c2_net_tm_fini(tm);

	/* TM1 stop and fini */
	c2_clink_add(&tm1->ntm_chan, &tmwait);
	rc = c2_net_tm_stop(tm1, false);
	C2_UT_ASSERT(rc == 0);
	c2_chan_wait(&tmwait);
	/* Check whether all buffers are returned to the pool. */
	C2_UT_ASSERT(tm1->ntm_recv_pool->nbp_free ==
		     tm1->ntm_recv_pool->nbp_buf_nr);
	c2_clink_del(&tmwait);
	c2_thread_join(&ut_tm_thread); /* cleanup thread */
	c2_thread_fini(&ut_tm_thread);
	c2_net_tm_fini(tm1);

	c2_clink_fini(&tmwait);
	/* Finalize the buffer pool. */
	c2_net_buffer_pool_lock(dom->nd_pool);
	c2_net_buffer_pool_fini(dom->nd_pool);
	c2_free(dom->nd_pool);

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
