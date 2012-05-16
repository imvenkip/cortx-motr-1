/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 */

#include "lib/arith.h"
#include "lib/assert.h"
#include "lib/cdefs.h"
#include "lib/misc.h"
#include "lib/thread.h"
#include "lib/ut.h"

#include "net/bulk_emulation/mem_xprt_xo.c"
#include "net/bulk_emulation/st/ping.c"

/* Create buffers with different shapes but same total size.
   Also create identical buffers for exact shape testing.
*/
enum { NR_BUFS = 10 };

static void test_buf_copy(void)
{
	static struct {
		uint32_t    num_segs;
		c2_bcount_t seg_size;
	} shapes[NR_BUFS] = {
		[0] = { 1, 48 },
		[1] = { 1, 48 },
		[2] = { 2, 24 },
		[3] = { 2, 24 },
		[4] = { 3, 16 },
		[5] = { 3, 16 },
		[6] = { 4, 12 },
		[7] = { 4, 12 },
		[8] = { 6,  8 },
		[9] = { 6,  8 },
	};
	static const char *msg = "abcdefghijklmnopqrstuvwxyz0123456789"
		"ABCDEFGHIJK";
	size_t msglen = strlen(msg)+1;
	static struct c2_net_buffer bufs[NR_BUFS];
	int i;
	struct c2_net_buffer *nb;

	C2_SET_ARR0(bufs);
	for (i = 0; i < NR_BUFS; ++i) {
		C2_UT_ASSERT(msglen == shapes[i].num_segs * shapes[i].seg_size);
		C2_UT_ASSERT(c2_bufvec_alloc(&bufs[i].nb_buffer,
					     shapes[i].num_segs,
					     shapes[i].seg_size) == 0);
	}
	nb = &bufs[0]; /* single buffer */
	C2_UT_ASSERT(nb->nb_buffer.ov_vec.v_nr == 1);
	memcpy(nb->nb_buffer.ov_buf[0], msg, msglen);
	C2_UT_ASSERT(memcmp(nb->nb_buffer.ov_buf[0], msg, msglen) == 0);
	for (i = 1; i < NR_BUFS; ++i) {
		int j;
		const char *p = msg;
		C2_UT_ASSERT(mem_copy_buffer(&bufs[i],&bufs[i-1],msglen) == 0);
		C2_UT_ASSERT(bufs[i].nb_length == 0); /* does not set field */
		for (j = 0; j < bufs[i].nb_buffer.ov_vec.v_nr; ++j) {
			int k;
			char *q;
			for (k = 0;
			     k < bufs[i].nb_buffer.ov_vec.v_count[j]; ++k) {
				q = bufs[i].nb_buffer.ov_buf[j] + k;
				C2_UT_ASSERT(*p++ == *q);
			}
		}

	}
	for (i = 0; i < NR_BUFS; ++i)
		c2_bufvec_free(&bufs[i].nb_buffer);
}

void tf_tm_cb1(const struct c2_net_tm_event *ev);
static void test_ep(void)
{
	/* dom1 */
	static struct c2_net_domain dom1 = {
		.nd_xprt = NULL
	};
	static struct c2_net_tm_callbacks tm_cbs1 = {
		.ntc_event_cb = tf_tm_cb1
	};
	static struct c2_net_transfer_mc d1tm1 = {
		.ntm_callbacks = &tm_cbs1,
		.ntm_state = C2_NET_TM_UNDEFINED
	};
	struct c2_clink tmwait;
	static struct c2_net_end_point *ep1;
	static struct c2_net_end_point *ep2;
	static struct c2_net_end_point *ep3;
	const char *addr;

	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_bulk_mem_xprt));
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));

	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait);
	C2_UT_ASSERT(!c2_net_tm_start(&d1tm1, "255.255.255.255:54321"));
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	C2_UT_ASSERT(d1tm1.ntm_ep != NULL);

	addr = "255.255.255.255:65535:4294967295";
	C2_UT_ASSERT(c2_net_end_point_create(&ep1, &d1tm1, addr) == -EINVAL);
	addr = "255.255.255.255:65535";
	C2_UT_ASSERT(!c2_net_end_point_create(&ep1, &d1tm1, addr));
	C2_UT_ASSERT(strcmp(ep1->nep_addr, addr) == 0);
	C2_UT_ASSERT(c2_atomic64_get(&ep1->nep_ref.ref_cnt) == 1);
	C2_UT_ASSERT(ep1->nep_addr != addr);

	C2_UT_ASSERT(!c2_net_end_point_create(&ep2, &d1tm1, addr));
	C2_UT_ASSERT(strcmp(ep2->nep_addr, addr) == 0);
	C2_UT_ASSERT(ep2->nep_addr != addr);
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 2);
	C2_UT_ASSERT(ep1 == ep2);

	C2_UT_ASSERT(!c2_net_end_point_create(&ep3, &d1tm1, addr));

	C2_UT_ASSERT(strcmp(ep3->nep_addr, "255.255.255.255:65535") == 0);
	C2_UT_ASSERT(strcmp(ep3->nep_addr, addr) == 0);
	C2_UT_ASSERT(ep3->nep_addr != addr);
	C2_UT_ASSERT(c2_atomic64_get(&ep3->nep_ref.ref_cnt) == 3);
	C2_UT_ASSERT(ep1 == ep3);

	C2_UT_ASSERT(!c2_net_end_point_put(ep1));
	C2_UT_ASSERT(!c2_net_end_point_put(ep2));
	C2_UT_ASSERT(!c2_net_end_point_put(ep3));

	c2_clink_add(&d1tm1.ntm_chan, &tmwait);
	C2_UT_ASSERT(!c2_net_tm_stop(&d1tm1, false));
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);

	c2_net_tm_fini(&d1tm1);
	c2_net_domain_fini(&dom1);
}

static enum c2_net_tm_ev_type cb_evt1;
static enum c2_net_queue_type cb_qt1;
static struct c2_net_buffer *cb_nb1;
static enum c2_net_tm_state cb_tms1;
static int32_t cb_status1;
void tf_tm_cb1(const struct c2_net_tm_event *ev)
{
	cb_evt1    = ev->nte_type;
	cb_nb1     = NULL;
	cb_qt1     = C2_NET_QT_NR;
	cb_tms1    = ev->nte_next_state;
	cb_status1 = ev->nte_status;
}

void tf_buf_cb1(const struct c2_net_buffer_event *ev)
{
	cb_evt1    = C2_NET_TEV_NR;
	cb_nb1     = ev->nbe_buffer;
	cb_qt1     = cb_nb1->nb_qtype;
	cb_tms1    = C2_NET_TM_UNDEFINED;
	cb_status1 = ev->nbe_status;
}

void tf_cbreset1(void)
{
	cb_evt1    = C2_NET_TEV_NR;
	cb_nb1     = NULL;
	cb_qt1     = C2_NET_QT_NR;
	cb_tms1    = C2_NET_TM_UNDEFINED;
	cb_status1 = 9999999;
}

static enum c2_net_tm_ev_type cb_evt2;
static enum c2_net_queue_type cb_qt2;
static struct c2_net_buffer *cb_nb2;
static enum c2_net_tm_state cb_tms2;
static int32_t cb_status2;
void tf_tm_cb2(const struct c2_net_tm_event *ev)
{
	cb_evt2    = ev->nte_type;
	cb_nb2     = NULL;
	cb_qt2     = C2_NET_QT_NR;
	cb_tms2    = ev->nte_next_state;
	cb_status2 = ev->nte_status;
}

void tf_buf_cb2(const struct c2_net_buffer_event *ev)
{
	cb_evt2    = C2_NET_TEV_NR;
	cb_nb2     = ev->nbe_buffer;
	cb_qt2     = cb_nb2->nb_qtype;
	cb_tms2    = C2_NET_TM_UNDEFINED;
	cb_status2 = ev->nbe_status;
}

void tf_cbreset2(void)
{
	cb_evt2    = C2_NET_TEV_NR;
	cb_nb2     = NULL;
	cb_qt2     = C2_NET_QT_NR;
	cb_tms2    = C2_NET_TM_UNDEFINED;
	cb_status2 = 9999999;
}

void tf_cbreset(void)
{
	tf_cbreset1();
	tf_cbreset2();
}

static void test_failure(void)
{
	/* some variables below are static to reduce kernel stack
	   consumption. */

	/* dom1 */
	static struct c2_net_domain dom1 = {
		.nd_xprt = NULL
	};
	static struct c2_net_tm_callbacks tm_cbs1 = {
		.ntc_event_cb = tf_tm_cb1
	};
	static struct c2_net_transfer_mc d1tm1 = {
		.ntm_callbacks = &tm_cbs1,
		.ntm_state = C2_NET_TM_UNDEFINED
	};
	static struct c2_net_buffer_callbacks buf_cbs1 = {
		.nbc_cb = {
			[C2_NET_QT_MSG_RECV]          = tf_buf_cb1,
			[C2_NET_QT_MSG_SEND]          = tf_buf_cb1,
			[C2_NET_QT_PASSIVE_BULK_RECV] = tf_buf_cb1,
			[C2_NET_QT_PASSIVE_BULK_SEND] = tf_buf_cb1,
			[C2_NET_QT_ACTIVE_BULK_RECV]  = tf_buf_cb1,
			[C2_NET_QT_ACTIVE_BULK_SEND]  = tf_buf_cb1,
		},
	};
	static struct c2_net_buffer d1nb1;
	static struct c2_net_buffer d1nb2;
	static struct c2_clink tmwait1;

	/* dom 2 */
	static struct c2_net_domain dom2 = {
		.nd_xprt = NULL
	};
	static const struct c2_net_tm_callbacks tm_cbs2 = {
		.ntc_event_cb = tf_tm_cb2
	};
	static struct c2_net_transfer_mc d2tm1 = {
		.ntm_callbacks = &tm_cbs2,
		.ntm_state = C2_NET_TM_UNDEFINED
	};
	static struct c2_net_transfer_mc d2tm2 = {
		.ntm_callbacks = &tm_cbs2,
		.ntm_state = C2_NET_TM_UNDEFINED
	};
	static const struct c2_net_buffer_callbacks buf_cbs2 = {
		.nbc_cb = {
			[C2_NET_QT_MSG_RECV]          = tf_buf_cb2,
			[C2_NET_QT_MSG_SEND]          = tf_buf_cb2,
			[C2_NET_QT_PASSIVE_BULK_RECV] = tf_buf_cb2,
			[C2_NET_QT_PASSIVE_BULK_SEND] = tf_buf_cb2,
			[C2_NET_QT_ACTIVE_BULK_RECV]  = tf_buf_cb2,
			[C2_NET_QT_ACTIVE_BULK_SEND]  = tf_buf_cb2,
		},
	};
	static struct c2_net_buffer d2nb1;
	static struct c2_net_buffer d2nb2;
	static c2_bcount_t d2nb2_len;
	static struct c2_clink tmwait2;

	static struct c2_net_end_point *ep;
	static struct c2_net_qstats qs;

	/* setup the first dom */
	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_bulk_mem_xprt));
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_start(&d1tm1, "127.0.0.1:10"));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STARTED);
	C2_UT_ASSERT(strcmp(d1tm1.ntm_ep->nep_addr, "127.0.0.1:10") == 0);
	C2_SET0(&d1nb1);
	C2_UT_ASSERT(!c2_bufvec_alloc(&d1nb1.nb_buffer, 4, 10));
	C2_UT_ASSERT(!c2_net_buffer_register(&d1nb1, &dom1));
	d1nb1.nb_callbacks = &buf_cbs1;
	C2_SET0(&d1nb2);
	C2_UT_ASSERT(!c2_bufvec_alloc(&d1nb2.nb_buffer, 1, 10));
	C2_UT_ASSERT(!c2_net_buffer_register(&d1nb2, &dom1));
	d1nb2.nb_callbacks = &buf_cbs1;

	/* setup the second dom */
	C2_UT_ASSERT(!c2_net_domain_init(&dom2, &c2_net_bulk_mem_xprt));
	C2_UT_ASSERT(!c2_net_tm_init(&d2tm1, &dom2));
	/* don't start the TM on port 20 yet */
	C2_UT_ASSERT(!c2_net_tm_init(&d2tm2, &dom2));
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm2.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_tm_start(&d2tm2, "127.0.0.1:21"));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(d2tm2.ntm_state == C2_NET_TM_STARTED);
	C2_UT_ASSERT(strcmp(d2tm2.ntm_ep->nep_addr, "127.0.0.1:21") == 0);

	C2_SET0(&d2nb1);
	C2_UT_ASSERT(!c2_bufvec_alloc(&d2nb1.nb_buffer, 4, 10));
	C2_UT_ASSERT(!c2_net_buffer_register(&d2nb1, &dom2));
	d2nb1.nb_callbacks = &buf_cbs2;
	C2_SET0(&d2nb2);
	C2_UT_ASSERT(!c2_bufvec_alloc(&d2nb2.nb_buffer, 1, 10));
	d2nb2_len = 1 * 10;
	C2_UT_ASSERT(!c2_net_buffer_register(&d2nb2, &dom2));
	d2nb2.nb_callbacks = &buf_cbs2;

	/* test failure situations */

	/* TEST
	   Send a message from d1tm1 to d2tm1 - should fail because
	   the destination TM not started.
	*/
	tf_cbreset();
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &d1tm1, "127.0.0.1:20"));
	C2_UT_ASSERT(strcmp(ep->nep_addr, "127.0.0.1:20") == 0);
	d1nb1.nb_qtype = C2_NET_QT_MSG_SEND;
	d1nb1.nb_ep = ep;
	d1nb1.nb_length = 10; /* don't care */
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_buffer_add(&d1nb1, &d1tm1));
	C2_UT_ASSERT(!c2_net_end_point_put(ep));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_MSG_SEND);
	C2_UT_ASSERT(cb_nb1 == &d1nb1);
	C2_UT_ASSERT(cb_status1 == -ENETUNREACH);

	/* start the TM on port 20 in the second dom */
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_tm_start(&d2tm1, "127.0.0.1:20"));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(d2tm1.ntm_state == C2_NET_TM_STARTED);
	C2_UT_ASSERT(strcmp(d2tm1.ntm_ep->nep_addr, "127.0.0.1:20") == 0);

	/* TEST
	   Send a message from d1tm1 to d2tm1 - should fail because
	   no receive buffers available.
	   The failure count on the receive queue of d2tm1 should
	   be bumped, and an -ENOBUFS error callback delivered.
	*/
	tf_cbreset();
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_MSG_RECV,&qs,true));
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);

	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_MSG_SEND,&qs,true));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &d1tm1, "127.0.0.1:20"));
	C2_UT_ASSERT(strcmp(ep->nep_addr, "127.0.0.1:20") == 0);
	d1nb1.nb_qtype = C2_NET_QT_MSG_SEND;
	d1nb1.nb_ep = ep;
	d1nb1.nb_length = 10; /* don't care */
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_buffer_add(&d1nb1, &d1tm1));
	C2_UT_ASSERT(!c2_net_end_point_put(ep));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_MSG_SEND);
	C2_UT_ASSERT(cb_nb1 == &d1nb1);
	C2_UT_ASSERT(cb_status1 == -ENOBUFS);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_MSG_SEND,&qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);

	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_MSG_RECV,&qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 0);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);
	C2_UT_ASSERT(cb_evt2 == C2_NET_TEV_ERROR);
	C2_UT_ASSERT(cb_status2 == -ENOBUFS);

	/* TEST
	   Add a receive buffer in d2tm1.
	   Send a larger message from d1tm1 to d2tm1.
	   Both buffers should fail with -EMSGSIZE.
	*/
	tf_cbreset();
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_MSG_RECV,&qs,true));
	d2nb2.nb_qtype = C2_NET_QT_MSG_RECV;
	d2nb2.nb_ep = NULL;
	d2nb2.nb_min_receive_size = d2nb2_len;
	d2nb2.nb_max_receive_msgs = 1;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_buffer_add(&d2nb2, &d2tm1));

	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_MSG_SEND,&qs,true));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &d1tm1, "127.0.0.1:20"));
	C2_UT_ASSERT(strcmp(ep->nep_addr, "127.0.0.1:20") == 0);
	d1nb1.nb_qtype = C2_NET_QT_MSG_SEND;
	d1nb1.nb_ep = ep;
	d1nb1.nb_length = 40;
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_buffer_add(&d1nb1, &d1tm1));
	C2_UT_ASSERT(!c2_net_end_point_put(ep));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_MSG_SEND);
	C2_UT_ASSERT(cb_nb1 == &d1nb1);
	C2_UT_ASSERT(cb_status1 == -EMSGSIZE);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_MSG_SEND,&qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);

	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_MSG_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb2);
	C2_UT_ASSERT(cb_status2 == -EMSGSIZE);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_MSG_RECV,&qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);

	/* TEST
	   Set up a passive receive buffer in one dom, and
	   try to actively receive from it.
	*/
	tf_cbreset();
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_PASSIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &d2tm1, "127.0.0.1:10"));
	C2_UT_ASSERT(strcmp(ep->nep_addr, "127.0.0.1:10") == 0);
	d2nb1.nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	d2nb1.nb_ep = ep;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_buffer_add(&d2nb1, &d2tm1));
	C2_UT_ASSERT(d2nb1.nb_desc.nbd_len != 0);
	C2_UT_ASSERT(!c2_net_end_point_put(ep));

	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_ACTIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(!c2_net_desc_copy(&d2nb1.nb_desc, &d1nb1.nb_desc));
	d1nb1.nb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
	d1nb1.nb_length = 10;
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_buffer_add(&d1nb1, &d1tm1));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_ACTIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb1 == &d1nb1);
	C2_UT_ASSERT(cb_status1 == -EPERM);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_ACTIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);
	c2_net_desc_free(&d1nb1.nb_desc);

	c2_net_buffer_del(&d2nb1, &d2tm1);
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb1);
	C2_UT_ASSERT(cb_status2 == -ECANCELED);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_PASSIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 1);
	c2_net_desc_free(&d2nb1.nb_desc);

	/* TEST
	   Set up a passive receive buffer in one dom, and
	   try to send a larger message from the other dom.
	*/
	tf_cbreset();
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_PASSIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &d2tm1, "127.0.0.1:10"));
	C2_UT_ASSERT(strcmp(ep->nep_addr, "127.0.0.1:10") == 0);
	d2nb2.nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	d2nb2.nb_ep = ep;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_buffer_add(&d2nb2, &d2tm1));
	C2_UT_ASSERT(d2nb2.nb_desc.nbd_len != 0);
	C2_UT_ASSERT(!c2_net_end_point_put(ep));

	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_ACTIVE_BULK_SEND,
					  &qs,true));
	C2_UT_ASSERT(!c2_net_desc_copy(&d2nb2.nb_desc, &d1nb1.nb_desc));
	d1nb1.nb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
	d1nb1.nb_length = 40; /* larger than d2nb2 */
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_buffer_add(&d1nb1, &d1tm1));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_ACTIVE_BULK_SEND);
	C2_UT_ASSERT(cb_nb1 == &d1nb1);
	C2_UT_ASSERT(cb_status1 == -EFBIG);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_ACTIVE_BULK_SEND,
					  &qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);
	c2_net_desc_free(&d1nb1.nb_desc);

	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb2);
	C2_UT_ASSERT(cb_status2 == -EFBIG);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_PASSIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);
	c2_net_desc_free(&d2nb2.nb_desc);

	/* TEST
	   Setup a passive send buffer and add it. Save the descriptor in the
	   active buffer of the other dom.  Do not start the active operation
	   yet. Del the passive buffer. Re-submit the same buffer for the same
	   passive operation. Try the active operation in the other dom, using
	   the original desc. Should fail because buffer id changes per add.
	 */
	tf_cbreset();
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_PASSIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &d2tm1, "127.0.0.1:10"));
	C2_UT_ASSERT(strcmp(ep->nep_addr, "127.0.0.1:10") == 0);
	d2nb1.nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	d2nb1.nb_ep = ep;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_buffer_add(&d2nb1, &d2tm1));
	C2_UT_ASSERT(d2nb1.nb_desc.nbd_len != 0);
	/* C2_UT_ASSERT(!c2_net_end_point_put(ep)); reuse it on resubmit */

	/* copy the desc but don't start the active operation yet */
	C2_UT_ASSERT(!c2_net_desc_copy(&d2nb1.nb_desc, &d1nb1.nb_desc));

	/* cancel the original passive operation */
	c2_net_buffer_del(&d2nb1, &d2tm1);
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb1);
	C2_UT_ASSERT(cb_status2 == -ECANCELED);
	c2_net_desc_free(&d2nb1.nb_desc);

	/* resubmit */
	tf_cbreset2();
	d2nb1.nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	d2nb1.nb_ep = ep;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_buffer_add(&d2nb1, &d2tm1));
	C2_UT_ASSERT(d2nb1.nb_desc.nbd_len != 0);
	C2_UT_ASSERT(!c2_net_end_point_put(ep));

	/* descriptors should have changed */
	C2_UT_ASSERT(d1nb1.nb_desc.nbd_len != d2nb1.nb_desc.nbd_len ||
		     memcmp(d1nb1.nb_desc.nbd_data, d2nb1.nb_desc.nbd_data,
			    d1nb1.nb_desc.nbd_len) != 0);

	/* start the active operation */
	tf_cbreset1();
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_ACTIVE_BULK_SEND,
					  &qs,true));
	d1nb1.nb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
	d1nb1.nb_length = 10;
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_buffer_add(&d1nb1, &d1tm1));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_ACTIVE_BULK_SEND);
	C2_UT_ASSERT(cb_nb1 == &d1nb1);
	C2_UT_ASSERT(cb_status1 == -ENOENT);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_ACTIVE_BULK_SEND,
					  &qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);
	c2_net_desc_free(&d1nb1.nb_desc);

	c2_net_buffer_del(&d2nb1, &d2tm1);
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb1);
	C2_UT_ASSERT(cb_status2 == -ECANCELED);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_PASSIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 2);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 2);
	C2_UT_ASSERT(qs.nqs_num_dels == 2);
	c2_net_desc_free(&d2nb1.nb_desc);

	/* fini */
	c2_net_buffer_deregister(&d1nb1, &dom1);
	c2_bufvec_free(&d1nb1.nb_buffer);
	c2_net_buffer_deregister(&d1nb2, &dom1);
	c2_bufvec_free(&d1nb2.nb_buffer);
	c2_net_buffer_deregister(&d2nb1, &dom2);
	c2_bufvec_free(&d2nb1.nb_buffer);
	c2_net_buffer_deregister(&d2nb2, &dom2);
	c2_bufvec_free(&d2nb2.nb_buffer);

	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_stop(&d1tm1, false));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STOPPED);

	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_tm_stop(&d2tm1, false));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(d2tm1.ntm_state == C2_NET_TM_STOPPED);

	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm2.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_tm_stop(&d2tm2, false));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(d2tm2.ntm_state == C2_NET_TM_STOPPED);

	c2_net_tm_fini(&d1tm1);
	c2_net_tm_fini(&d2tm1);
	c2_net_tm_fini(&d2tm2);

	c2_net_domain_fini(&dom1);
	c2_net_domain_fini(&dom2);
}

enum {
	PING_CLIENT_SEGMENTS = 8,
	PING_CLIENT_SEGMENT_SIZE = 512,
	PING_SERVER_SEGMENTS = 4,
	PING_SERVER_SEGMENT_SIZE = 1024,
	PING_NR_BUFS = 20
};
static int quiet_printf(const char *fmt, ...)
{
	return 0;
}

static struct ping_ops quiet_ops = {
    .pf = quiet_printf
};

static void test_ping(void)
{
	/* some variables below are static to reduce kernel stack
	   consumption. */

	static struct ping_ctx cctx = {
		.pc_ops = &quiet_ops,
		.pc_xprt = &c2_net_bulk_mem_xprt,
		.pc_nr_bufs = PING_NR_BUFS,
		.pc_segments = PING_CLIENT_SEGMENTS,
		.pc_seg_size = PING_CLIENT_SEGMENT_SIZE,
		.pc_tm = {
			.ntm_state     = C2_NET_TM_UNDEFINED
		}
	};
	static struct ping_ctx sctx = {
		.pc_ops = &quiet_ops,
		.pc_xprt = &c2_net_bulk_mem_xprt,
		.pc_nr_bufs = PING_NR_BUFS,
		.pc_segments = PING_SERVER_SEGMENTS,
		.pc_seg_size = PING_SERVER_SEGMENT_SIZE,
		.pc_tm = {
			.ntm_state     = C2_NET_TM_UNDEFINED
		}
	};
	int rc;
	struct c2_net_end_point *server_ep;
	struct c2_thread server_thread;
	int i;
	char *data;
	int len;

	c2_mutex_init(&sctx.pc_mutex);
	c2_cond_init(&sctx.pc_cond);
	c2_mutex_init(&cctx.pc_mutex);
	c2_cond_init(&cctx.pc_cond);

	C2_UT_ASSERT(c2_net_xprt_init(&c2_net_bulk_mem_xprt) == 0);

	C2_UT_ASSERT(ping_client_init(&cctx, &server_ep) == 0);
	/* client times out because server is not ready */
	C2_UT_ASSERT(ping_client_msg_send_recv(&cctx, server_ep, NULL) != 0);
	/* server runs in background thread */
	C2_SET0(&server_thread);
	rc = C2_THREAD_INIT(&server_thread, struct ping_ctx *, NULL,
			    &ping_server, &sctx, "ping_server");
	if (rc != 0) {
		C2_UT_FAIL("failed to start ping server");
		return;
	} else
		C2_UT_PASS("started ping server");

	C2_UT_ASSERT(ping_client_msg_send_recv(&cctx, server_ep, NULL) == 0);
	C2_UT_ASSERT(ping_client_passive_recv(&cctx, server_ep) == 0);
	C2_UT_ASSERT(ping_client_passive_send(&cctx, server_ep, NULL) == 0);

	/* test sending/receiving a bigger payload */
	data = c2_alloc(PING_CLIENT_SEGMENTS * PING_CLIENT_SEGMENT_SIZE);
	C2_UT_ASSERT(data != NULL);
	len = (PING_CLIENT_SEGMENTS-1) * PING_CLIENT_SEGMENT_SIZE + 1;
	for (i = 0; i < len; ++i)
		data[i] = "abcdefghi"[i % 9];
	C2_UT_ASSERT(ping_client_msg_send_recv(&cctx, server_ep, data) == 0);
	C2_UT_ASSERT(ping_client_passive_send(&cctx, server_ep, data) == 0);

	C2_UT_ASSERT(ping_client_fini(&cctx, server_ep) == 0);

	ping_server_should_stop(&sctx);
	C2_UT_ASSERT(c2_thread_join(&server_thread) == 0);

	c2_cond_fini(&cctx.pc_cond);
	c2_mutex_fini(&cctx.pc_mutex);
	c2_cond_fini(&sctx.pc_cond);
	c2_mutex_fini(&sctx.pc_mutex);
	c2_net_xprt_fini(&c2_net_bulk_mem_xprt);
	c2_free(data);
}

static void ntc_event_callback(const struct c2_net_tm_event *ev)
{
}

static void test_tm(void)
{
	static struct c2_net_domain dom1 = {
		.nd_xprt = NULL
	};
	const struct c2_net_tm_callbacks cbs1 = {
		.ntc_event_cb = ntc_event_callback
	};
	struct c2_net_transfer_mc d1tm1 = {
		.ntm_callbacks = &cbs1,
		.ntm_state = C2_NET_TM_UNDEFINED
	};
	struct c2_clink tmwait1;

	/* should be able to init/fini a dom back-to-back */
	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_bulk_mem_xprt));
	c2_net_domain_fini(&dom1);

	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_bulk_mem_xprt));
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));

	/* should be able to fini it immediately */
	c2_net_tm_fini(&d1tm1);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_UNDEFINED);

	/* should be able to init it again */
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_INITIALIZED);
	C2_UT_ASSERT(c2_list_contains(&dom1.nd_tms, &d1tm1.ntm_dom_linkage));

	/* check thread counts */
	C2_UT_ASSERT(c2_net_bulk_mem_tm_get_num_threads(&d1tm1) == 1);
	c2_net_bulk_mem_tm_set_num_threads(&d1tm1, 2);
	C2_UT_ASSERT(c2_net_bulk_mem_tm_get_num_threads(&d1tm1) == 2);

	/* fini */
	if (d1tm1.ntm_state > C2_NET_TM_INITIALIZED) {
		c2_clink_init(&tmwait1, NULL);
		c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
		C2_UT_ASSERT(!c2_net_tm_stop(&d1tm1, false));
		c2_chan_wait(&tmwait1);
		c2_clink_del(&tmwait1);
		C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STOPPED);
	}
	c2_net_tm_fini(&d1tm1);
	c2_net_domain_fini(&dom1);
}

const struct c2_test_suite c2_net_bulk_mem_ut = {
        .ts_name = "net-bulk-mem",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "net_bulk_mem_buf_copy_test", test_buf_copy },
		{ "net_bulk_mem_tm_test",       test_tm },
                { "net_bulk_mem_ep",            test_ep },
                { "net_bulk_mem_failure_tests", test_failure },
                { "net_bulk_mem_ping_tests",    test_ping },
                { NULL, NULL }
        }
};
C2_EXPORTED(c2_net_bulk_mem_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
