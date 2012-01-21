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
 * Original author: Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 *                  Carl Braganza <Carl_Braganza@xyratex.com>
 * Original creation date: 1/4/2012
 */

#include "net/lnet/lnet_main.c"
#include "lib/ut.h"

static bool ut_chan_timedwait(struct c2_clink *link, uint32_t secs)
{
	c2_time_t timeout;
	timeout = c2_time_from_now(secs,0);
	return c2_chan_timedwait(link, timeout);
}

#ifdef __KERNEL__
#include "net/lnet/ut/linux_kernel/klnet_ut.c"
#endif

static int test_lnet_init(void)
{
	return c2_net_xprt_init(&c2_net_lnet_xprt);
}

static int test_lnet_fini(void)
{
	c2_net_xprt_fini(&c2_net_lnet_xprt);
	return 0;
}

static void test_tm_initfini(void)
{
	static struct c2_net_domain dom1 = {
		.nd_xprt = NULL
	};
	const struct c2_net_tm_callbacks cbs1 = {
		.ntc_event_cb = LAMBDA(void,(const struct c2_net_tm_event *ev) {
				       }),
	};
	struct c2_net_transfer_mc d1tm1 = {
		.ntm_callbacks = &cbs1,
		.ntm_state = C2_NET_TM_UNDEFINED
	};

	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_lnet_xprt));
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));

	/* should be able to fini it immediately */
	c2_net_tm_fini(&d1tm1);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_UNDEFINED);

	/* should be able to init it again */
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_INITIALIZED);
	C2_UT_ASSERT(c2_list_contains(&dom1.nd_tms, &d1tm1.ntm_dom_linkage));

	/* fini */
	c2_net_tm_fini(&d1tm1);
	c2_net_domain_fini(&dom1);
}

static enum c2_net_tm_ev_type ecb_evt;
static enum c2_net_tm_state ecb_tms;
static int32_t ecb_status;
static int ecb_count;
static void ecb_reset(void)
{
	ecb_evt = C2_NET_TEV_NR;
	ecb_tms = C2_NET_TM_UNDEFINED;
	ecb_status = 1;
	ecb_count = 0;
}

static void tf_tm_ecb(const struct c2_net_tm_event *ev)
{
	ecb_evt    = ev->nte_type;
	ecb_tms    = ev->nte_next_state;
	ecb_status = ev->nte_status;
	ecb_count++;
}

enum {
	STARTSTOP_DOM_NR = 3,
	STARTSTOP_PID = 12345,	/* same as LUSTRE_SRV_LNET_PID */
	STARTSTOP_PORTAL = 30,
};
#ifdef __KERNEL__
/* LUSTRE_SRV_LNET_PID macro is not available in user space */
C2_BASSERT(STARTSTOP_PID == LUSTRE_SRV_LNET_PID);
#endif

static void test_tm_startstop(void)
{
	struct c2_net_domain *dom;
	struct c2_net_transfer_mc *tm;
	const struct c2_net_tm_callbacks cbs1 = {
		.ntc_event_cb = tf_tm_ecb,
	};
	static struct c2_clink tmwait1;
	char * const *nidstrs;
	char epstr[C2_NET_LNET_XEP_ADDR_LEN];
	char dyn_epstr[C2_NET_LNET_XEP_ADDR_LEN];
	char save_epstr[C2_NET_LNET_XEP_ADDR_LEN];
	struct c2_bitmap procs;
	int i;

	C2_ALLOC_PTR(dom);
	C2_ALLOC_PTR(tm);
	C2_UT_ASSERT(dom != NULL && tm != NULL);
	tm->ntm_callbacks = &cbs1;

	C2_UT_ASSERT(!c2_net_domain_init(dom, &c2_net_lnet_xprt));
	C2_UT_ASSERT(!c2_net_lnet_ifaces_get(&nidstrs));
	C2_UT_ASSERT(nidstrs != NULL && nidstrs[0] != NULL);
	sprintf(epstr, "%s:%d:%d:101",
		nidstrs[0], STARTSTOP_PID, STARTSTOP_PORTAL);
	sprintf(dyn_epstr, "%s:%d:%d:*",
		nidstrs[0], STARTSTOP_PID, STARTSTOP_PORTAL);
	c2_net_lnet_ifaces_put(&nidstrs);
	C2_UT_ASSERT(nidstrs == NULL);
	C2_UT_ASSERT(!c2_net_tm_init(tm, dom));

	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&tm->ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_start(tm, epstr));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(ecb_count == 1);
	C2_UT_ASSERT(ecb_evt == C2_NET_TEV_STATE_CHANGE);
	C2_UT_ASSERT(ecb_tms == C2_NET_TM_STARTED);
	C2_UT_ASSERT(ecb_status == 0);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STARTED);
	if (tm->ntm_state == C2_NET_TM_FAILED) {
		/* skip rest of this test, else C2_ASSERT will occur */
		c2_net_tm_fini(tm);
		c2_net_domain_fini(dom);
		c2_free(tm);
		c2_free(dom);
		C2_UT_FAIL("aborting test case, endpoint in-use?");
		return;
	}
	C2_UT_ASSERT(strcmp(tm->ntm_ep->nep_addr, epstr) == 0);

	ecb_reset();
	c2_clink_add(&tm->ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_stop(tm, false));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(ecb_count == 1);
	C2_UT_ASSERT(ecb_evt == C2_NET_TEV_STATE_CHANGE);
	C2_UT_ASSERT(ecb_tms == C2_NET_TM_STOPPED);
	C2_UT_ASSERT(ecb_status == 0);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STOPPED);
	c2_net_tm_fini(tm);
	c2_net_domain_fini(dom);
	c2_free(tm);
	c2_free(dom);

	/* test combination of dynamic endpoint, start with confine,
	 * and multiple domains and TMs
	 */
	C2_ALLOC_ARR(dom, STARTSTOP_DOM_NR);
	C2_ALLOC_ARR(tm, STARTSTOP_DOM_NR);
	C2_UT_ASSERT(dom != NULL && tm != NULL);

	for (i = 0; i < STARTSTOP_DOM_NR; ++i) {
		tm[i].ntm_callbacks = &cbs1;
		C2_UT_ASSERT(!c2_net_domain_init(&dom[i], &c2_net_lnet_xprt));
		C2_UT_ASSERT(!c2_net_tm_init(&tm[i], &dom[i]));
		C2_UT_ASSERT(c2_bitmap_init(&procs, 1) == 0);
		c2_bitmap_set(&procs, 0, true);
		C2_UT_ASSERT(c2_net_tm_confine(&tm[i], &procs) == 0);

		ecb_reset();
		c2_clink_add(&tm[i].ntm_chan, &tmwait1);
		C2_UT_ASSERT(!c2_net_tm_start(&tm[i], dyn_epstr));
		c2_chan_wait(&tmwait1);
		c2_clink_del(&tmwait1);
		C2_UT_ASSERT(ecb_tms == C2_NET_TM_STARTED);
		C2_UT_ASSERT(tm[i].ntm_state == C2_NET_TM_STARTED);
		C2_UT_ASSERT(strcmp(tm[i].ntm_ep->nep_addr, dyn_epstr) != 0);
		if (i > 0)
			C2_UT_ASSERT(strcmp(tm[i].ntm_ep->nep_addr,
					    tm[i-1].ntm_ep->nep_addr) < 0);
	}

	/* subtest: dynamic TMID reuse using middle TM */
	strcpy(save_epstr, tm[1].ntm_ep->nep_addr);
	c2_clink_add(&tm[1].ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_stop(&tm[1], false));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(tm[1].ntm_state == C2_NET_TM_STOPPED);
	c2_net_tm_fini(&tm[1]);
	C2_UT_ASSERT(!c2_net_tm_init(&tm[1], &dom[1]));

	c2_clink_add(&tm[1].ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_start(&tm[1], dyn_epstr));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(ecb_tms == C2_NET_TM_STARTED);
	C2_UT_ASSERT(tm[1].ntm_state == C2_NET_TM_STARTED);
	C2_UT_ASSERT(strcmp(tm[1].ntm_ep->nep_addr, save_epstr) == 0);

	for (i = 0; i < STARTSTOP_DOM_NR; ++i) {
		c2_clink_add(&tm[i].ntm_chan, &tmwait1);
		C2_UT_ASSERT(!c2_net_tm_stop(&tm[i], false));
		c2_chan_wait(&tmwait1);
		c2_clink_del(&tmwait1);
		C2_UT_ASSERT(ecb_tms == C2_NET_TM_STOPPED);
		C2_UT_ASSERT(tm[i].ntm_state == C2_NET_TM_STOPPED);
		c2_net_tm_fini(&tm[i]);
		c2_net_domain_fini(&dom[i]);
	}
	c2_free(tm);
	c2_free(dom);
}


static enum c2_net_queue_type cb_qt1;
static struct c2_net_buffer *cb_nb1;
static int32_t cb_status1;

static void ut_buf_cb1(const struct c2_net_buffer_event *ev)
{
	cb_nb1     = ev->nbe_buffer;
	cb_qt1     = cb_nb1->nb_qtype;
	cb_status1 = ev->nbe_status;
}

static void ut_cbreset1(void)
{
	cb_nb1     = NULL;
	cb_qt1     = C2_NET_QT_NR;
	cb_status1 = 9999999;
}

static enum c2_net_queue_type cb_qt2;
static struct c2_net_buffer *cb_nb2;
static int32_t cb_status2;

static void ut_buf_cb2(const struct c2_net_buffer_event *ev)
{
	cb_nb2     = ev->nbe_buffer;
	cb_qt2     = cb_nb2->nb_qtype;
	cb_status2 = ev->nbe_status;
}

static void ut_cbreset2(void)
{
	cb_nb2     = NULL;
	cb_qt2     = C2_NET_QT_NR;
	cb_status2 = 9999999;
}

static void ut_cbreset(void)
{
	ut_cbreset1();
	ut_cbreset2();
}

static char ut_line_buf[1024];
#define zUT(x,label) { int rc = x; if(rc!=0) { sprintf(ut_line_buf,"%s %d: %s: %d",__FILE__,__LINE__,#x,rc); C2_UT_FAIL(ut_line_buf); goto label;}}

static void test_msg(void) {
#define TM_BUFS1    1
#define TM_BUFSEGS1 2
#define TM_BUFS2    1
#define TM_BUFSEGS2 1
#define TM_MSG_SIZE 1024

	struct test_msg_data {
		struct c2_net_tm_callbacks     tmcb;
		struct c2_net_domain           dom1;
		struct c2_net_transfer_mc      tm1;
		struct c2_clink                tmwait1;
		struct c2_net_buffer_callbacks buf_cb1;
		struct c2_net_buffer           bufs1[TM_BUFS1];
		struct c2_net_domain           dom2;
		struct c2_net_transfer_mc      tm2;
		struct c2_clink                tmwait2;
		struct c2_net_buffer_callbacks buf_cb2;
		struct c2_net_buffer           bufs2[TM_BUFS2];
		struct c2_net_qstats           qs;
	} *td;
	char * const *nidstrs = NULL;
	int i;
	int rc;
	struct c2_net_domain      *dom1;
	struct c2_net_transfer_mc *tm1;
	struct c2_net_buffer      *nb1;
	struct c2_net_domain      *dom2;
	struct c2_net_transfer_mc *tm2;
	struct c2_net_buffer      *nb2;

	/*
	  Setup.
	 */
	C2_ALLOC_PTR(td);
	C2_UT_ASSERT(td != NULL);
	if (td == NULL)
		return;
	dom1 = &td->dom1;
	tm1  = &td->tm1;
	dom2 = &td->dom2;
	tm2  = &td->tm2;
	c2_clink_init(&td->tmwait1, NULL);
	c2_clink_init(&td->tmwait2, NULL);
	td->tmcb.ntc_event_cb = tf_tm_ecb;
	tm1->ntm_callbacks = &td->tmcb;
	tm2->ntm_callbacks = &td->tmcb;
	for (i = C2_NET_QT_MSG_RECV; i < C2_NET_QT_NR; ++i) {
		td->buf_cb1.nbc_cb[i] = ut_buf_cb1;
		td->buf_cb2.nbc_cb[i] = ut_buf_cb2;
	}

	C2_UT_ASSERT(!c2_net_lnet_ifaces_get(&nidstrs));
	C2_UT_ASSERT(nidstrs != NULL && nidstrs[0] != NULL);

	C2_UT_ASSERT(!c2_net_domain_init(dom1, &c2_net_lnet_xprt));
	{
		char epstr[C2_NET_LNET_XEP_ADDR_LEN];
		c2_bcount_t max_seg_size;

		max_seg_size = c2_net_domain_get_max_buffer_segment_size(dom1);
		C2_UT_ASSERT(max_seg_size > 0);
		C2_UT_ASSERT(max_seg_size >= TM_MSG_SIZE);
		for (i=0; i < TM_BUFS1; ++i) {
			nb1 = &td->bufs1[i];
			rc = c2_bufvec_alloc(&nb1->nb_buffer, TM_BUFSEGS1,
					     max_seg_size);
			C2_UT_ASSERT(rc == 0);
			if (rc != 0) {
				C2_UT_FAIL("aborting: buf alloc failed");
				goto dereg1;
			}
			rc = c2_net_buffer_register(nb1, dom1);
			if (rc != 0) {
				C2_UT_FAIL("aborting: buf reg failed");
				goto dereg1;
			}
			C2_UT_ASSERT(nb1->nb_flags & C2_NET_BUF_REGISTERED);
			nb1->nb_callbacks = &td->buf_cb1;
		}

		C2_UT_ASSERT(!c2_net_tm_init(tm1, dom1));

		sprintf(epstr, "%s:%d:%d:*",
			nidstrs[0], STARTSTOP_PID, STARTSTOP_PORTAL);
		c2_clink_add(&tm1->ntm_chan, &td->tmwait1);
		C2_UT_ASSERT(!c2_net_tm_start(tm1, epstr));
		c2_chan_wait(&td->tmwait1);
		c2_clink_del(&td->tmwait1);
		C2_UT_ASSERT(tm1->ntm_state == C2_NET_TM_STARTED);
		if (tm1->ntm_state == C2_NET_TM_FAILED) {
			C2_UT_FAIL("aborting: tm1 startup failed");
			goto fini1;
		}
		printk("tm1: %s\n", tm1->ntm_ep->nep_addr);
	}

	C2_UT_ASSERT(!c2_net_domain_init(dom2, &c2_net_lnet_xprt));
	{
		char epstr[C2_NET_LNET_XEP_ADDR_LEN];
		c2_bcount_t max_seg_size;

		max_seg_size = c2_net_domain_get_max_buffer_segment_size(dom2);
		C2_UT_ASSERT(max_seg_size > 0);
		C2_UT_ASSERT(max_seg_size >= TM_MSG_SIZE);
		for (i=0; i < TM_BUFS2; ++i) {
			nb2 = &td->bufs2[i];
			rc = c2_bufvec_alloc(&nb2->nb_buffer, TM_BUFSEGS2,
					     max_seg_size);
			C2_UT_ASSERT(rc == 0);
			if (rc != 0) {
				C2_UT_FAIL("aborting: buf alloc failed");
				goto dereg2;
			}
			rc = c2_net_buffer_register(nb2, dom2);
			if (rc != 0) {
				C2_UT_FAIL("aborting: buf reg failed");
				goto dereg2;
			}
			C2_UT_ASSERT(nb2->nb_flags & C2_NET_BUF_REGISTERED);
			nb2->nb_callbacks = &td->buf_cb2;
		}

		C2_UT_ASSERT(!c2_net_tm_init(tm2, dom2));

		sprintf(epstr, "%s:%d:%d:*",
			nidstrs[0], STARTSTOP_PID, STARTSTOP_PORTAL);
		c2_clink_add(&tm2->ntm_chan, &td->tmwait2);
		C2_UT_ASSERT(!c2_net_tm_start(tm2, epstr));
		c2_chan_wait(&td->tmwait2);
		c2_clink_del(&td->tmwait2);
		C2_UT_ASSERT(tm2->ntm_state == C2_NET_TM_STARTED);
		if (tm2->ntm_state == C2_NET_TM_FAILED) {
			C2_UT_FAIL("aborting: tm2 startup failed");
			goto fini2;
		}
		printk("tm2: %s\n", tm2->ntm_ep->nep_addr);
	}

	/*
	  TEST
	  Add a buffer for message receive then cancel it.
	 */
	nb1 = &td->bufs1[0];
	nb1->nb_min_receive_size = TM_MSG_SIZE;
	nb1->nb_max_receive_msgs = 1;
	nb1->nb_qtype = C2_NET_QT_MSG_RECV;
#ifdef __KERNEL__
	_ktest_umd_init(tm1, nb1);
#endif
	ut_cbreset();
	zUT(c2_net_buffer_add(nb1, tm1), teardown);

	c2_clink_add(&tm1->ntm_chan, &td->tmwait1);
	c2_net_buffer_del(nb1, tm1);
	printk("after buffer del\n");
	ut_chan_timedwait(&td->tmwait1, 30);
	c2_clink_del(&td->tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_MSG_RECV);
	C2_UT_ASSERT(cb_nb1 == nb1);
	C2_UT_ASSERT(cb_status1 == -ECANCELED);
	C2_UT_ASSERT(!c2_net_tm_stats_get(tm1, C2_NET_QT_PASSIVE_BULK_RECV,
					  &td->qs, true));
	C2_UT_ASSERT(td->qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(td->qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(td->qs.nqs_num_adds == 1);
	C2_UT_ASSERT(td->qs.nqs_num_dels == 1);

	/*
	  Teardown
	*/
 teardown:
	c2_clink_add(&tm2->ntm_chan, &td->tmwait2);
	C2_UT_ASSERT(!c2_net_tm_stop(tm2, false));
	c2_chan_wait(&td->tmwait2);
	c2_clink_del(&td->tmwait2);
	C2_UT_ASSERT(tm2->ntm_state == C2_NET_TM_STOPPED);
 fini2:
	c2_net_tm_fini(&td->tm2);
 dereg2:
	for (i=0; i < TM_BUFS2; ++i) {
		nb2 = &td->bufs2[i];
		if (nb2->nb_buffer.ov_vec.v_nr == 0)
			continue;
		c2_net_buffer_deregister(nb2, dom2);
		c2_bufvec_free(&nb2->nb_buffer);
	}
	c2_net_domain_fini(&td->dom2);

	c2_clink_add(&tm1->ntm_chan, &td->tmwait1);
	C2_UT_ASSERT(!c2_net_tm_stop(tm1, false));
	c2_chan_wait(&td->tmwait1);
	c2_clink_del(&td->tmwait1);
	C2_UT_ASSERT(tm1->ntm_state == C2_NET_TM_STOPPED);
 fini1:
	c2_net_tm_fini(&td->tm1);
 dereg1:
	for (i=0; i < TM_BUFS1; ++i) {
		nb1 = &td->bufs1[i];
		if (nb1->nb_buffer.ov_vec.v_nr == 0)
			continue;
		c2_net_buffer_deregister(nb1, dom1);
		c2_bufvec_free(&nb1->nb_buffer);
	}
	c2_net_domain_fini(&td->dom1);

	if (nidstrs)
		c2_net_lnet_ifaces_put(&nidstrs);
	C2_UT_ASSERT(nidstrs == NULL);
	c2_clink_fini(&td->tmwait1);
	c2_clink_fini(&td->tmwait2);
	c2_free(td);

	/* XXX PUT UNDEFS HERE */
}

const struct c2_test_suite c2_net_lnet_ut = {
        .ts_name = "net-lnet",
        .ts_init = test_lnet_init,
        .ts_fini = test_lnet_fini,
        .ts_tests = {
#ifdef __KERNEL__
		{ "net_lnet_buf_shape (K)", ktest_buf_shape },
		{ "net_lnet_buf_reg (K)",   ktest_buf_reg },
		{ "net_lnet_ep_addr (K)",   ktest_core_ep_addr },
		{ "net_lnet_enc_dec (K)",   ktest_enc_dec },
#endif
		{ "net_lnet_tm_initfini",   test_tm_initfini },
		{ "net_lnet_tm_startstop",  test_tm_startstop },
		{ "net_lnet_msg",           test_msg },
                { NULL, NULL }
        }
};
C2_EXPORTED(c2_net_lnet_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
