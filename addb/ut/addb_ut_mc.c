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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 10/11/2012
 */

/* This file is designed to be included by addb/ut/addb_ut.c */

/*
 ****************************************************************************
 * Test common ADDB machine interfaces with fake components.
 ****************************************************************************
 */

/*
 * Fake event manager component.
 */
static int addb_ut_mc_evm_cnt;
static void addb_ut_mc_evm_get(struct m0_addb_mc *mc,
			       struct m0_addb_mc_evmgr *mgr)
{
	++addb_ut_mc_evm_cnt;
	M0_UT_ASSERT(addb_ut_mc_evm_cnt > 0);
}

static void addb_ut_mc_evm_put(struct m0_addb_mc *mc,
			       struct m0_addb_mc_evmgr *mgr)
{
	M0_UT_ASSERT(addb_ut_mc_evm_cnt > 0);
	--addb_ut_mc_evm_cnt;
}

static struct m0_addb_rec *addb_ut_mc_evm_rec_alloc(struct m0_addb_mc *mc,
						    size_t len)
{
	return m0_alloc(len);
}

static void addb_ut_mc_evm_post(struct m0_addb_mc  *mc, struct m0_addb_rec *rec)
{
	m0_free(rec);
}

static struct m0_addb_mc_evmgr addb_ut_mc_evmgr = {
	.evm_get       = addb_ut_mc_evm_get,
	.evm_put       = addb_ut_mc_evm_put,
	.evm_rec_alloc = addb_ut_mc_evm_rec_alloc,
	.evm_post      = addb_ut_mc_evm_post,
};

/* fake constructor */
static void addb_ut_mc_configure_evmgr(struct m0_addb_mc *mc)
{
	M0_UT_ASSERT(!m0_addb_mc_has_evmgr(mc));
	mc->am_evmgr = &addb_ut_mc_evmgr;
	(*mc->am_evmgr->evm_get)(mc, mc->am_evmgr); /* incr ref count */
}

/*
 * Fake passthrough variant of the event manager.
 */
static struct m0_addb_rec *addb_ut_mc_pt_evm_rec_alloc(struct m0_addb_mc *mc,
						       size_t len)
{
	return mc->am_sink->rs_rec_alloc(mc, len);
}

static void addb_ut_mc_pt_evm_post(struct m0_addb_mc  *mc,
				   struct m0_addb_rec *rec)
{
	mc->am_sink->rs_save(mc, rec);
}

static struct m0_addb_mc_evmgr addb_ut_mc_pt_evmgr = {
	.evm_get       = addb_ut_mc_evm_get,
	.evm_put       = addb_ut_mc_evm_put,
	.evm_rec_alloc = addb_ut_mc_pt_evm_rec_alloc,
	.evm_post      = addb_ut_mc_pt_evm_post,
};

/* fake constructor */
static void addb_ut_mc_configure_pt_evmgr(struct m0_addb_mc *mc)
{
	M0_UT_ASSERT(!m0_addb_mc_has_evmgr(mc));
	M0_UT_ASSERT(m0_addb_mc_has_recsink(mc));
	mc->am_evmgr = &addb_ut_mc_pt_evmgr;
	(*mc->am_evmgr->evm_get)(mc, mc->am_evmgr); /* incr ref count */
}

/*
 * Fake record sink component.
 */
static int addb_ut_mc_rs_cnt;
static void addb_ut_mc_rs_get(struct m0_addb_mc *mc,
			      struct m0_addb_mc_recsink *snk)
{
	++addb_ut_mc_rs_cnt;
	M0_UT_ASSERT(addb_ut_mc_rs_cnt > 0);
}

static void addb_ut_mc_rs_put(struct m0_addb_mc *mc,
			      struct m0_addb_mc_recsink *snk)
{
	M0_UT_ASSERT(addb_ut_mc_rs_cnt > 0);
	--addb_ut_mc_rs_cnt;
}

static struct m0_mutex addb_ut_mc_rs_mutex;
static struct m0_addb_rec *addb_ut_mc_rs_rec_mem;
static void (*addb_ut_mc_rs_save_cb)(const struct m0_addb_rec *rec);
static size_t addb_ut_mc_rs_rec_len;
static struct m0_addb_rec *addb_ut_mc_rs_rec_alloc(struct m0_addb_mc *mc,
						   size_t len)
{
	m0_mutex_lock(&addb_ut_mc_rs_mutex);
	M0_UT_ASSERT(addb_ut_mc_rs_rec_mem == NULL);
	addb_ut_mc_rs_rec_mem = m0_alloc(len);
	addb_ut_mc_rs_rec_len = len;
	return addb_ut_mc_rs_rec_mem;
}

static void addb_ut_mc_rs_save(struct m0_addb_mc  *mc, struct m0_addb_rec *rec)
{
	M0_UT_ASSERT(m0_mutex_is_locked(&addb_ut_mc_rs_mutex));
	if (ut_cache_evmgr_idx == 0)
		M0_UT_ASSERT(rec == addb_ut_mc_rs_rec_mem);
	if (addb_ut_mc_rs_save_cb)
		(*addb_ut_mc_rs_save_cb)(rec);
	m0_free(rec);
	addb_ut_mc_rs_rec_mem = NULL;
	addb_ut_mc_rs_rec_len = 0;
	m0_mutex_unlock(&addb_ut_mc_rs_mutex);
}

static struct m0_addb_mc_recsink addb_ut_mc_recsink = {
	.rs_get       = addb_ut_mc_rs_get,
	.rs_put       = addb_ut_mc_rs_put,
	.rs_rec_alloc = addb_ut_mc_rs_rec_alloc,
	.rs_save      = addb_ut_mc_rs_save,
};

/* reset */
static void addb_ut_mc_reset(void)
{
	addb_ut_mc_evm_cnt = 0;
	addb_ut_mc_rs_cnt = 0;
	addb_ut_mc_rs_rec_mem = NULL;
	addb_ut_mc_rs_rec_len = 0;
	addb_ut_mc_rs_save_cb = NULL;
}

/* fake constructor */
static void addb_ut_mc_configure_recsink(struct m0_addb_mc *mc)
{
	M0_UT_ASSERT(!m0_addb_mc_has_recsink(mc));
	mc->am_sink = &addb_ut_mc_recsink;
	(*mc->am_sink->rs_get)(mc, mc->am_sink);
}

/*
 * Test basic machine subs
 */
static void addb_ut_mc_test(void)
{
	struct m0_addb_mc mc1 = { 0 };
	struct m0_addb_mc mc2 = { 0 };
	int evmcnt = 0;
	int rscnt = 0;

	/*
	 * Configure mc1 with evmgr before recsink.
	 */
	M0_UT_ASSERT(!m0_addb_mc_is_initialized(&mc1));
	m0_addb_mc_init(&mc1);
	M0_UT_ASSERT(m0_addb_mc_is_initialized(&mc1));

	M0_UT_ASSERT(!m0_addb_mc_is_configured(&mc1));
	M0_UT_ASSERT(!m0_addb_mc_is_fully_configured(&mc1));
	addb_ut_mc_configure_evmgr(&mc1);
	++evmcnt;
	M0_UT_ASSERT(addb_ut_mc_evm_cnt == evmcnt);
	M0_UT_ASSERT(m0_addb_mc_is_configured(&mc1));
	M0_UT_ASSERT(m0_addb_mc_has_evmgr(&mc1));
	M0_UT_ASSERT(!m0_addb_mc_is_fully_configured(&mc1));
	addb_ut_mc_configure_recsink(&mc1);
	++rscnt;
	M0_UT_ASSERT(addb_ut_mc_rs_cnt == rscnt);
	M0_UT_ASSERT(m0_addb_mc_has_recsink(&mc1));
	M0_UT_ASSERT(m0_addb_mc_has_evmgr(&mc1));
	M0_UT_ASSERT(m0_addb_mc_is_configured(&mc1));
	M0_UT_ASSERT(m0_addb_mc_is_fully_configured(&mc1));

	/*
	 * Configure mc2 with recsink before evmgr.
	 */
	M0_UT_ASSERT(!m0_addb_mc_is_initialized(&mc2));
	m0_addb_mc_init(&mc2);
	M0_UT_ASSERT(m0_addb_mc_is_initialized(&mc2));

	M0_UT_ASSERT(!m0_addb_mc_is_configured(&mc2));
	M0_UT_ASSERT(!m0_addb_mc_is_fully_configured(&mc2));
	addb_ut_mc_configure_recsink(&mc2);
	++rscnt;
	M0_UT_ASSERT(addb_ut_mc_rs_cnt == rscnt);
	M0_UT_ASSERT(m0_addb_mc_has_recsink(&mc2));
	M0_UT_ASSERT(!m0_addb_mc_is_fully_configured(&mc2));
	addb_ut_mc_configure_evmgr(&mc2);
	++evmcnt;
	M0_UT_ASSERT(addb_ut_mc_evm_cnt == evmcnt);
	M0_UT_ASSERT(m0_addb_mc_is_configured(&mc2));
	M0_UT_ASSERT(m0_addb_mc_has_evmgr(&mc2));
	M0_UT_ASSERT(m0_addb_mc_has_recsink(&mc2));
	M0_UT_ASSERT(m0_addb_mc_is_configured(&mc2));
	M0_UT_ASSERT(m0_addb_mc_is_fully_configured(&mc2));

	/* the global machine must be initialized */
	M0_UT_ASSERT(m0_addb_mc_is_initialized(&m0_addb_gmc));

	/*
	 * initialize the global machine as a dup of mc2
	 */
	m0_addb_mc_dup(&mc2, &m0_addb_gmc);
	M0_UT_ASSERT(addb_ut_mc_evm_cnt == evmcnt + 1);
	M0_UT_ASSERT(addb_ut_mc_rs_cnt == rscnt + 1);
	M0_UT_ASSERT(m0_addb_mc_has_recsink(&m0_addb_gmc));
	M0_UT_ASSERT(m0_addb_mc_has_evmgr(&m0_addb_gmc));
	M0_UT_ASSERT(m0_addb_mc_is_configured(&m0_addb_gmc));
	M0_UT_ASSERT(m0_addb_mc_is_fully_configured(&m0_addb_gmc));

	/* fini */
	m0_addb_mc_fini(&mc1);
	M0_UT_ASSERT(!m0_addb_mc_is_initialized(&mc1));
	M0_UT_ASSERT(addb_ut_mc_evm_cnt > 0);
	M0_UT_ASSERT(addb_ut_mc_rs_cnt > 0);

	m0_addb_mc_fini(&mc2);
	M0_UT_ASSERT(!m0_addb_mc_is_initialized(&mc2));
	M0_UT_ASSERT(addb_ut_mc_evm_cnt > 0);
	M0_UT_ASSERT(addb_ut_mc_rs_cnt > 0);

	m0_addb_mc_fini(&m0_addb_gmc);
	M0_UT_ASSERT(!m0_addb_mc_is_initialized(&m0_addb_gmc));
	M0_UT_ASSERT(addb_ut_mc_evm_cnt == 0);
	M0_UT_ASSERT(addb_ut_mc_rs_cnt == 0);

	/* restore the global machine for other UTs */
	m0_addb_mc_init(&m0_addb_gmc);
	M0_UT_ASSERT(m0_addb_mc_is_initialized(&m0_addb_gmc));
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
