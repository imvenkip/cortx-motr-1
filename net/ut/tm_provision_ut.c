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
static int ut_tm_prov_event_cb_calls = 0;
void ut_tm_prov_event_cb(const struct c2_net_tm_event *ev)
{
	ut_tm_prov_event_cb_calls++;
}

/* UT transfer machine */
struct c2_net_tm_callbacks ut_tm_prov_cb = {
	.ntc_event_cb = ut_tm_prov_event_cb
};

static void ut_prov_msg_recv_cb(const struct c2_net_buffer_event *ev)
{
	struct c2_net_buffer	*nb;
	struct c2_tl	     *ql;
	struct c2_net_transfer_mc *tm;
	int rc;
	C2_PRE(ev != NULL && ev->nbe_buffer != NULL);
	nb = ev->nbe_buffer;
	tm = nb->nb_tm;
	C2_PRE(tm->ntm_recv_pool != NULL && nb->nb_pool != NULL);
	ql = &tm->ntm_q[C2_NET_QT_MSG_RECV];
	
	if (nb->nb_tm->ntm_state == C2_NET_TM_STARTED &&
	  !(nb->nb_flags & C2_NET_BUF_RETAIN))
		rc = c2_net_buffer_add(nb, tm);
	else if(nb->nb_tm->ntm_state == C2_NET_TM_STOPPED ||
		nb->nb_tm->ntm_state == C2_NET_TM_STOPPING ||
		nb->nb_tm->ntm_state == C2_NET_TM_FAILED) {
	//		c2_tlist_for(&tm_tl, ql, nb) {
				c2_net_buffer_pool_lock(tm->ntm_recv_pool);
				c2_net_buffer_pool_put(tm->ntm_recv_pool, nb,
			      			       tm->ntm_pool_colour);
				c2_net_buffer_pool_unlock(tm->ntm_recv_pool);
	//		} c2_tlist_endfor;
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

struct c2_net_buffer_callbacks ut_buf_prov_multi_use_cb = {
	.nbc_cb = {
		[C2_NET_QT_MSG_RECV]          = ut_multi_use_cb,
		[C2_NET_QT_MSG_SEND]          = ut_multi_use_cb,
		[C2_NET_QT_PASSIVE_BULK_RECV] = ut_multi_use_cb,
		[C2_NET_QT_PASSIVE_BULK_SEND] = ut_multi_use_cb,
		[C2_NET_QT_ACTIVE_BULK_RECV]  = ut_multi_use_cb,
		[C2_NET_QT_ACTIVE_BULK_SEND]  = ut_multi_use_cb,
	},
};

int c2_ut_xprt_buffer_min_recv_size(struct c2_net_domain *ndom)
{
	return 1<<12;
}

int c2_ut_xprt_buffer_max_recv_msgs(struct c2_net_domain *ndom)
{
	return max_recv_msgs;
}

static void low(struct c2_net_buffer_pool *bp)
{
	/* Buffer pool is below threshold.  */
}

static const struct c2_net_buffer_pool_ops b_ops = {
	.nbpo_not_empty	      = c2_net_domain_buffer_pool_not_empty,
	.nbpo_below_threshold = low,
};
static bool ut_tm_prov_stop_called = false;
static int ut_tm_prov_stop(struct c2_net_transfer_mc *tm, bool cancel)
{
	int rc;
	struct c2_clink tmwait;
	struct c2_net_buffer	*nb;
	struct c2_tl	     *ql;
	c2_clink_init(&tmwait, NULL);
	ql = &tm->ntm_q[C2_NET_QT_MSG_RECV];
	
	C2_UT_ASSERT(c2_mutex_is_locked(&tm->ntm_mutex));
	c2_tlist_for(&tm_tl, ql, nb) {
		ut_buf_del_called = false;

		c2_clink_add(&tm->ntm_chan, &tmwait);
		c2_mutex_unlock(&tm->ntm_mutex);
		c2_net_buffer_del(nb, tm);
		C2_UT_ASSERT(ut_buf_del_called);
		//	wait on channel for post (and consume UT thread)
		c2_chan_wait(&tmwait);
		c2_clink_del(&tmwait);
		rc = c2_thread_join(&ut_del_thread);
		C2_UT_ASSERT(rc == 0);
		c2_mutex_lock(&tm->ntm_mutex);
	} c2_tlist_endfor;

	ut_tm_prov_stop_called = true;
/*	rc = C2_THREAD_INIT(&ut_tm_thread, int, NULL,
			    &ut_post_state_change_ev_thread, C2_NET_TM_STOPPED,
			    "state_change%d", C2_NET_TM_STOPPED);
	C2_UT_ASSERT(rc == 0);
	return rc;
	*/
	rc = ut_tm_stop(tm, false);
	return rc;
}

static void test_net_tm_prov(void)
{
	int rc;//, i;
	c2_bcount_t buf_size, buf_seg_size;
	int32_t   buf_segs;
	struct c2_net_domain *dom = &utdom;
	struct c2_net_transfer_mc *tm = &ut_tm;
//	struct c2_net_buffer *nbs;
	//struct c2_net_buffer *nb;
	struct c2_clink tmwait;
	static uint32_t		   tm_colours;
	
	c2_time_t c2tt_to_period;
	
	ut_tm.ntm_callbacks = &ut_tm_prov_cb;
	ut_xprt_ops.xo_tm_stop = ut_tm_prov_stop;
	/* initialize the domain */
	C2_UT_ASSERT(ut_dom_init_called == false);
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
	c2_net_buffer_pool_init(dom->nd_pool, dom, 2, buf_segs, buf_seg_size, 64);
	c2_net_buffer_pool_lock(dom->nd_pool);
	rc = c2_net_buffer_pool_provision(dom->nd_pool, 32);
	c2_net_buffer_pool_unlock(dom->nd_pool);
	C2_UT_ASSERT(rc == 32);

	/* register the buffers */
/*	for (i = 0; i < C2_NET_QT_NR; ++i) {
		nb = c2_net_buffer_pool_get(tm->ntm_recv_pool,
					    tm->ntm_pool_colour);
		C2_UT_ASSERT(nb != NULL);
	}
*/
	c2_time_set(&c2tt_to_period, 120, 0); /* 2 min */

	/* TM init with callbacks */
	rc = c2_net_tm_init(tm, dom);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_init_called);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_INITIALIZED);
	C2_UT_ASSERT(c2_list_contains(&dom->nd_tms, &tm->ntm_dom_linkage));
	
	c2_net_tm_colour_set(tm, tm_colours++);

	rc = c2_net_tm_pool_attach(tm, dom->nd_pool, &ut_buf_prov_cb,
				   c2_ut_xprt_buffer_min_recv_size(dom),
				   c2_ut_xprt_buffer_max_recv_msgs(dom));

	/* TM start */
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&tm->ntm_chan, &tmwait);

	C2_UT_ASSERT(ut_end_point_create_called == false);
	rc = c2_net_tm_start(tm, "addr2");
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_start_called);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STARTING ||
		     tm->ntm_state == C2_NET_TM_STARTED);

	/* wait on channel for started */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	C2_UT_ASSERT(ut_tm_prov_event_cb_calls == 1);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STARTED);
	C2_UT_ASSERT(ut_end_point_create_called);

	C2_UT_ASSERT(tm->ntm_ep != NULL);
	C2_UT_ASSERT(c2_atomic64_get(&tm->ntm_ep->nep_ref.ref_cnt) == 1);
	
	/* clean up; real xprt would handle this itself */
	c2_thread_join(&ut_tm_thread);
	c2_thread_fini(&ut_tm_thread);

	/* add MSG_RECV buf - should succeeded as now started */
/*	nb = &nbs[C2_NET_QT_MSG_RECV];
	nb->nb_callbacks = &ut_buf_prov_cb;
	C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_QUEUED));
	nb->nb_qtype = C2_NET_QT_MSG_RECV;
	nb->nb_timeout = c2_time_add(c2_time_now(), c2tt_to_period);
	nb->nb_min_receive_size = buf_size;
	nb->nb_max_receive_msgs = 1;
	rc = c2_net_buffer_add(nb, tm);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_buf_add_called);
	C2_UT_ASSERT(nb->nb_flags & C2_NET_BUF_QUEUED);
	C2_UT_ASSERT(nb->nb_tm == tm);
	
	ut_buf_del_called = false;
	c2_clink_add(&tm->ntm_chan, &tmwait);
	c2_net_buffer_del(nb, tm);
	C2_UT_ASSERT(ut_buf_del_called);

//	wait on channel for post (and consume UT thread)
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	rc = c2_thread_join(&ut_del_thread);
	C2_UT_ASSERT(rc == 0);
*/
/*	ut_tm_prov_stop_called = false;
	rc = ut_tm_prov_stop(tm, false);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_prov_stop_called);
*/	
	/* TM stop and fini */
	c2_clink_add(&tm->ntm_chan, &tmwait);
	rc = c2_net_tm_stop(tm, false);
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	c2_thread_join(&ut_tm_thread); /* cleanup thread */
	c2_thread_fini(&ut_tm_thread);
	c2_clink_fini(&tmwait);
	

	/* de-register and free buffers */
/*	for (i = 0; i < C2_NET_QT_NR; ++i) {
		nb = &nbs[i];
		ut_buf_deregister_called = false;
		c2_net_buffer_deregister(nb, dom);
		C2_UT_ASSERT(ut_buf_deregister_called);
		C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_REGISTERED));
		c2_bufvec_free(&nb->nb_buffer);
	}
	c2_free(nbs);
*/	
	c2_net_tm_fini(tm);
	
	c2_net_buffer_pool_lock(dom->nd_pool);
	c2_net_buffer_pool_fini(dom->nd_pool);
	c2_free(dom->nd_pool);

	/* fini the domain */
	C2_UT_ASSERT(ut_dom_fini_called == false);
	c2_net_domain_fini(dom);
	C2_UT_ASSERT(ut_dom_fini_called);
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
