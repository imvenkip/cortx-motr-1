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

#ifdef __KERNEL__
#include "net/lnet/ut/linux_kernel/klnet_ut.c"
#endif

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

static void test_tm_startstop(void)
{
	static struct c2_net_domain dom1 = {
		.nd_xprt = NULL
	};
	const struct c2_net_tm_callbacks cbs1 = {
		.ntc_event_cb = tf_tm_ecb,
	};
	struct c2_net_transfer_mc d1tm1 = {
		.ntm_callbacks = &cbs1,
		.ntm_state = C2_NET_TM_UNDEFINED
	};
	static struct c2_clink tmwait1;
	char **nidstrs;
	char epstr[C2_NET_LNET_XEP_ADDR_LEN];
	struct c2_bitmap procs;

	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_lnet_xprt));
	C2_UT_ASSERT(!c2_net_lnet_ifaces_get(&dom1, &nidstrs));
	C2_UT_ASSERT(nidstrs != NULL && nidstrs[0] != NULL);
	sprintf(epstr, "%s:12345:30:10", nidstrs[0]);
	c2_net_lnet_ifaces_put(&dom1, nidstrs);
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));

	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_start(&d1tm1, epstr));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(ecb_count == 1);
	C2_UT_ASSERT(ecb_evt == C2_NET_TEV_STATE_CHANGE);
	C2_UT_ASSERT(ecb_tms == C2_NET_TM_STARTED);
	C2_UT_ASSERT(ecb_status == 0);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STARTED);
	if (d1tm1.ntm_state == C2_NET_TM_FAILED) {
		/* skip rest of this test, else C2_ASSERT will occur */
		c2_net_tm_fini(&d1tm1);
		c2_net_domain_fini(&dom1);
		C2_UT_FAIL("aborting test case, endpoint in-use?");
		return;
	}
	C2_UT_ASSERT(strcmp(d1tm1.ntm_ep->nep_addr, epstr) == 0);

	ecb_reset();
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_stop(&d1tm1, false));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(ecb_count == 1);
	C2_UT_ASSERT(ecb_evt == C2_NET_TEV_STATE_CHANGE);
	C2_UT_ASSERT(ecb_tms == C2_NET_TM_STOPPED);
	C2_UT_ASSERT(ecb_status == 0);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STOPPED);
	c2_net_tm_fini(&d1tm1);

	/* test combination of start with confine */
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));
	C2_UT_ASSERT(c2_bitmap_init(&procs, 1) == 0);
	c2_bitmap_set(&procs, 0, true);
	C2_UT_ASSERT(c2_net_tm_confine(&d1tm1, &procs) == 0);

	ecb_reset();
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_start(&d1tm1, epstr));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(ecb_tms == C2_NET_TM_STARTED);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STARTED);

	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_stop(&d1tm1, false));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(ecb_tms == C2_NET_TM_STOPPED);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STOPPED);
	c2_net_tm_fini(&d1tm1);

	c2_net_domain_fini(&dom1);
}

static void test_ep(void)
{
}

static void test_failure(void)
{
}

const struct c2_test_suite c2_net_lnet_ut = {
        .ts_name = "net-lnet",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
#ifdef __KERNEL__
		{ "net_lnet_ep_addr (K)",   ktest_core_ep_addr },
#endif
		{ "net_lnet_tm_initfini",   test_tm_initfini },
		{ "net_lnet_tm_startstop",  test_tm_startstop },
                { "net_lnet_ep",            test_ep },
                { "net_lnet_failure_tests", test_failure },
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
