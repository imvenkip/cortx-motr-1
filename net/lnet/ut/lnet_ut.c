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
 * Original author: Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 *                  Carl Braganza <Carl_Braganza@xyratex.com>
 * Original creation date: 1/4/2012
 */

#include "net/lnet/lnet_main.c"
#include "lib/ut.h"

#include "db/db.h" /* c2_dbenv, c2_table */

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
	STARTSTOP_STAT_SECS = 5,
	STARTSTOP_STAT_PER_PERIOD = 1,
	STARTSTOP_STAT_BUF_NR = 4,
};
#ifdef __KERNEL__
/* LUSTRE_SRV_LNET_PID macro is not available in user space */
C2_BASSERT(STARTSTOP_PID == LUSTRE_SRV_LNET_PID);
#endif

static struct c2_table mock_table;
static struct c2_dbenv mock_dbenv;
static int lnet_stat_ev_count;

int mock_db_add(struct c2_addb_dp *dp, struct c2_dbenv *dbenv,
		struct c2_table *db)
{
	if (dp->ad_ev == &nlx_qstat) {
		const struct c2_addb_ev_ops *ops = dp->ad_ev->ae_ops;
		struct nlx_qstat_body *body;
		struct c2_addb_record rec;
		struct nlx_addb_dp *ndp;

		ndp = container_of(dp, struct nlx_addb_dp, ad_dp);
		C2_UT_ASSERT(dp->ad_name != NULL);
		C2_UT_ASSERT(strncmp(dp->ad_name, "nlx_tm_stats:", 13) == 0);
		C2_UT_ASSERT(ndp->ad_qs != NULL);

		/* test the pack and getsize ops */
		C2_SET0(&rec);
		C2_UT_ASSERT(ops == &nlx_addb_qstats);
		rec.ar_data.cmb_count = ops->aeo_getsize(dp);
		C2_UT_ASSERT(rec.ar_data.cmb_count != 0);
		rec.ar_data.cmb_value = c2_alloc(rec.ar_data.cmb_count);
		C2_ASSERT(rec.ar_data.cmb_value != NULL);
		C2_UT_ASSERT(ops->aeo_pack(dp, &rec) == 0);
		body = (struct nlx_qstat_body *)rec.ar_data.cmb_value;
		C2_UT_ASSERT(body->sb_qid == dp->ad_rc);
		C2_UT_ASSERT(body->sb_qstats.nqs_num_adds ==
			     STARTSTOP_STAT_BUF_NR);
		c2_free(rec.ar_data.cmb_value);

		lnet_stat_ev_count++;
	}
	return 0;
}

static void test_tm_startstop(void)
{
	struct c2_net_domain *dom;
	struct c2_net_transfer_mc *tm;
	const struct c2_net_qstats fake_stats = {
		.nqs_num_adds = STARTSTOP_STAT_BUF_NR,
		.nqs_num_dels = STARTSTOP_STAT_BUF_NR,
		.nqs_num_s_events = STARTSTOP_STAT_BUF_NR,
	};
	const struct c2_net_tm_callbacks cbs1 = {
		.ntc_event_cb = tf_tm_ecb,
	};
	static struct c2_clink tmwait1;
	char * const *nidstrs;
	char epstr[C2_NET_LNET_XEP_ADDR_LEN];
	char dyn_epstr[C2_NET_LNET_XEP_ADDR_LEN];
	char save_epstr[C2_NET_LNET_XEP_ADDR_LEN];
	struct c2_bitmap procs;
	c2_time_t sleeptime;
	int rc;
	int i;

	C2_ALLOC_PTR(dom);
	C2_ALLOC_PTR(tm);
	C2_UT_ASSERT(dom != NULL && tm != NULL);
	tm->ntm_callbacks = &cbs1;

	/* mock addb db store */
	rc = c2_addb_choose_store_media(C2_ADDB_REC_STORE_DB,
					mock_db_add, &mock_table, &mock_dbenv);
	C2_UT_ASSERT(rc == 0);

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
	c2_net_lnet_tm_stat_interval_set(tm, STARTSTOP_STAT_SECS);

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
		rc = c2_addb_choose_store_media(C2_ADDB_REC_STORE_NONE);
		C2_UT_ASSERT(rc == 0);
		C2_UT_FAIL("aborting test case, endpoint in-use?");
		return;
	}
	C2_UT_ASSERT(strcmp(tm->ntm_ep->nep_addr, epstr) == 0);

	/* also test periodic statistics */
	C2_UT_ASSERT(lnet_stat_ev_count == 0);
	tm->ntm_qstats[C2_NET_QT_MSG_RECV] = fake_stats;
	c2_time_set(&sleeptime, STARTSTOP_STAT_SECS + 1, 0);
	C2_UT_ASSERT(c2_nanosleep(sleeptime, NULL) == 0);
	C2_UT_ASSERT(lnet_stat_ev_count == STARTSTOP_STAT_PER_PERIOD);
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
	C2_UT_ASSERT(lnet_stat_ev_count == 2 * STARTSTOP_STAT_PER_PERIOD);
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
	rc = c2_addb_choose_store_media(C2_ADDB_REC_STORE_NONE);
	C2_UT_ASSERT(rc == 0);
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
#endif
		{ "net_lnet_tm_initfini",   test_tm_initfini },
		{ "net_lnet_tm_startstop",  test_tm_startstop },
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
