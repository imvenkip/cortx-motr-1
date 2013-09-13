/*-*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rajanikant Chirmade <rajanikant_chirmade@xyratex.com>
 * Original creation date: 03/04/2013
 */

#include "rpc/rpc_machine.h"
#include "rpc/item_source.h"
#include "rpc/conn.h"
#include "lib/finject.h"
#ifndef __KERNEL__
#include "rpc/ut/clnt_srv_ctx.c"
#endif

extern const struct m0_tl_descr rec_queue_tl;

enum {
	ADDB_UT_RPCSINK_TS_INIT_PAGES = 16,
	ADDB_UT_RPCSINK_TS_MAX_PAGES  = 36,
	ADDB_UT_RPCSINK_TS_PAGE_SIZE  = 4096, /* bytes */
	ADDB_UT_RPCSINK_LOOP_COUNTER = 100
};

uint32_t               reclen[6];
struct m0_rpc_item    *item[4];
struct m0_addb_mc      mc;
struct m0_rpc_machine  rm;
struct m0_addb_ctx     ctx;

static uint32_t
addb_ut_rpcsink_fop_records_count(struct m0_rpc_item *item)
{
	struct m0_fop *fop = container_of(item, struct m0_fop, f_item);
	struct m0_addb_rpc_sink_fop *fop_data;

	fop_data = (struct m0_addb_rpc_sink_fop *)m0_fop_data(fop);
	return fop_data->arsf_nr;
}

static uint32_t addb_ut_rpcsink_get_last_reclen(struct rpcsink *rsink)
{
	struct m0_addb_ts_rec_header *header;

	header = rec_queue_tlist_tail(&rsink->rs_ts.at_rec_queue);
	return ADDB_TS_GET_REC_SIZE(header);
}

static struct m0_rpc_item_source *
create_fake_rpcsink_item_source(struct rpcsink *rsink)
{
        struct rpcsink_item_source *rpcsink_item_source;
        int                         rc;

        M0_ALLOC_PTR(rpcsink_item_source);
	M0_UT_ASSERT(rpcsink_item_source != NULL);

        rc = rpcsink_item_source_init(rsink, rpcsink_item_source);
        M0_UT_ASSERT(rc == 0);

	return &rpcsink_item_source->ris_source;
}

static void destroy_fake_rpcsink_item_source(struct m0_rpc_item_source *src)
{
	struct rpcsink_item_source *rpcsink_item_source =
		container_of(src, struct rpcsink_item_source, ris_source);

	rpcsink_item_source_fini(rpcsink_item_source);
	m0_free(rpcsink_item_source);
}

static void addb_ut_rpcsink_test(void)
{
	struct rpcsink            *rsink;
	struct m0_rpc_item_source *src;
	struct m0_addb_rec_type   *dp = &m0__addb_ut_rt_dp1;
	struct m0_addb_ctx        *cv[4] = { NULL, &m0_addb_proc_ctx,
					     &m0_addb_node_ctx, NULL };
	int                        rc;
	uint32_t		   fop_rec_count;
	uint32_t		   rec_count;
	uint32_t		   submitted;
	uint32_t		   ts_pages;

	m0_addb_mc_init(&mc);

	/*
	 * Test 01: Call m0_addb_mc_configure_rpc_sink(),
	 *          RPC sink object allocation failed.
	 */
	m0_fi_enable_once("m0_addb_mc_configure_rpc_sink",
			  "rsink_allocation_failed");
	rc = m0_addb_mc_configure_rpc_sink(&mc, &rm, NULL,
					   ADDB_UT_RPCSINK_TS_INIT_PAGES,
					   ADDB_UT_RPCSINK_TS_MAX_PAGES,
					   ADDB_UT_RPCSINK_TS_PAGE_SIZE);
	M0_UT_ASSERT(rc != 0);
	M0_UT_ASSERT(!m0_addb_mc_has_rpc_sink(&mc));

	/*
	 * Test 02: Call m0_addb_mc_configure_rpc_sink(),
	 *          Transient store initialization failed.
	 */
	m0_fi_enable_once("m0_addb_mc_configure_rpc_sink",
			  "addb_ts_init_failed");
	rc = m0_addb_mc_configure_rpc_sink(&mc, &rm, NULL,
					   ADDB_UT_RPCSINK_TS_INIT_PAGES,
					   ADDB_UT_RPCSINK_TS_MAX_PAGES,
					   ADDB_UT_RPCSINK_TS_PAGE_SIZE);
	M0_UT_ASSERT(rc != 0);
	M0_UT_ASSERT(!m0_addb_mc_has_rpc_sink(&mc));

	/*
	 * Test 03: Call m0_addb_mc_configure_rpc_sink(),
	 *          ADDB item sources add failed.
	 */
	m0_fi_enable_once("m0_addb_mc_configure_rpc_sink",
			  "item_source_registration_failed");
	rc = m0_addb_mc_configure_rpc_sink(&mc, &rm, NULL,
					   ADDB_UT_RPCSINK_TS_INIT_PAGES,
					   ADDB_UT_RPCSINK_TS_MAX_PAGES,
					   ADDB_UT_RPCSINK_TS_PAGE_SIZE);
	M0_UT_ASSERT(rc != 0);
	M0_UT_ASSERT(!m0_addb_mc_has_rpc_sink(&mc));

	/*
	 * Test 04: Call m0_addb_mc_configure_rpc_sink(),
	 *          which configures record sink & transient store.
	 */
	m0_fi_enable_once("m0_addb_mc_configure_rpc_sink",
			  "skip_item_source_registration");
	rc = m0_addb_mc_configure_rpc_sink(&mc, &rm, NULL,
					   ADDB_UT_RPCSINK_TS_INIT_PAGES,
					   ADDB_UT_RPCSINK_TS_MAX_PAGES,
					   ADDB_UT_RPCSINK_TS_PAGE_SIZE);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_addb_mc_has_rpc_sink(&mc));

	rsink = rpcsink_from_mc(&mc);
	M0_UT_ASSERT(rsink != NULL);
	M0_UT_ASSERT(rsink->rs_ts.at_pages.ov_vec.v_nr ==
		     ADDB_UT_RPCSINK_TS_INIT_PAGES);
	M0_UT_ASSERT(rsink->rs_ts.at_max_pages ==
		     ADDB_UT_RPCSINK_TS_MAX_PAGES);
	M0_UT_ASSERT(rsink->rs_ts.at_page_size ==
		     ADDB_UT_RPCSINK_TS_PAGE_SIZE);

	src = create_fake_rpcsink_item_source(rsink);
	M0_UT_ASSERT(src != NULL);

	/*
	 * Test 05: Call rpcsink_has_item() on empty transient store.
	 */
	rec_count = rec_queue_tlist_length(&rsink->rs_ts.at_rec_queue);
	M0_UT_ASSERT(rec_count == 0);
	M0_UT_ASSERT(!rpcsink_has_item(src));

	/* Post some of ADDB records to populate transient store */
	m0_addb_mc_configure_pt_evmgr(&mc);

	m0__addb_ut_ct0.act_magic = 0;
	m0__addb_ut_ct0.act_id = addb_ct_max_id + 1;
	m0_addb_ctx_type_register(&m0__addb_ut_ct0);
	dp->art_magic = 0;
	dp->art_id = addb_rt_max_id + 1;

	m0_addb_rec_type_register(dp);
	M0_ADDB_CTX_INIT(&mc, &ctx, &m0__addb_ut_ct0, &m0_addb_proc_ctx);
	reclen[0] = addb_ut_rpcsink_get_last_reclen(rsink);
	cv[0] = &ctx;

	M0_ADDB_POST(&mc, dp, cv, 1);
	reclen[1] = addb_ut_rpcsink_get_last_reclen(rsink);

	/*
	 * Test 06: rpcsink_alloc() extends transient store by
	 * RPCSINK_TS_EXT_PAGES page if it not able to extend by default value.
	 */
	M0_UT_ASSERT(rsink->rs_ts.at_pages.ov_vec.v_nr ==
		     ADDB_UT_RPCSINK_TS_INIT_PAGES);

	m0_fi_enable_once("rpcsink_alloc", "extend_ts");
	ts_pages = rsink->rs_ts.at_pages.ov_vec.v_nr;
	M0_ADDB_POST(&mc, dp, cv, 1);
	reclen[2] = addb_ut_rpcsink_get_last_reclen(rsink);

	M0_UT_ASSERT(rsink->rs_ts.at_pages.ov_vec.v_nr ==
		     ts_pages + RPCSINK_TS_EXT_PAGES);

	/*
	 * Test 07: rpcsink_alloc() extends transient store by 1 page
	 *          if it not able to extend by default value.
	 */
	m0_fi_enable_once("rpcsink_alloc", "extend_ts");
	ts_pages = rsink->rs_ts.at_pages.ov_vec.v_nr;
	M0_ADDB_POST(&mc, dp, cv, 1);
	reclen[3] = addb_ut_rpcsink_get_last_reclen(rsink);

	M0_UT_ASSERT(rsink->rs_ts.at_pages.ov_vec.v_nr == ts_pages + 1);

	M0_ADDB_POST(&mc, dp, cv, 1);
	reclen[4] = addb_ut_rpcsink_get_last_reclen(rsink);
	M0_ADDB_POST(&mc, dp, cv, 1);
	reclen[5] = addb_ut_rpcsink_get_last_reclen(rsink);

	/*
	 * Test 08: Call rpcsink_has_item() on non-empty transient store.
	 */
	rec_count = rec_queue_tlist_length(&rsink->rs_ts.at_rec_queue);
	M0_UT_ASSERT(rec_count == 6);

	M0_UT_ASSERT(rpcsink_has_item(src));

	/*
	 * Test 09: Call rpcsink_get_item(), get transient records failed.
	 */
	m0_fi_enable_once("rpcsink_get_item", "get_records_failed");
	item[0] = rpcsink_get_item(src, reclen[0]);
	M0_UT_ASSERT(item[0] == NULL);

	rec_count = rec_queue_tlist_length(&rsink->rs_ts.at_rec_queue);
	M0_UT_ASSERT(rec_count == 6);

	/*
	 * Test 10: Call rpcsink_get_item(), ADDB FOP preperation failed.
	 */
	m0_fi_enable_once("rpcsink_get_item", "fop_prepare_failed");
	item[0] = rpcsink_get_item(src, reclen[0]);
	M0_UT_ASSERT(item[0] == NULL);

	rec_count = rec_queue_tlist_length(&rsink->rs_ts.at_rec_queue);
	M0_UT_ASSERT(rec_count == 6);

	/*
	 * Test 11: Call rpcsink_get_item()
	 *          with a single ADDB record in RPC item.
	 */
	item[0] = rpcsink_get_item(src, reclen[0]);
	M0_UT_ASSERT(item[0] != NULL);

	fop_rec_count = addb_ut_rpcsink_fop_records_count(item[0]);
	M0_UT_ASSERT(fop_rec_count == 1);

	submitted = rpcsink_trans_rec_tlist_length(&rsink->rs_rpc_submitted);
	M0_UT_ASSERT(submitted == 1);

	rec_count = rec_queue_tlist_length(&rsink->rs_ts.at_rec_queue);
	M0_UT_ASSERT(rec_count == 5);

	/*
	 * Test 12: Call rpcsink_get_item()
	 *          with multiple ADDB records in RPC item.
	 */
	item[1] = rpcsink_get_item(src, reclen[1]+reclen[2]);
	M0_UT_ASSERT(item[1] != NULL);

	fop_rec_count = addb_ut_rpcsink_fop_records_count(item[1]);
	M0_UT_ASSERT(fop_rec_count == 2);

	submitted = rpcsink_trans_rec_tlist_length(&rsink->rs_rpc_submitted);
	M0_UT_ASSERT(submitted == 3);

	rec_count = rec_queue_tlist_length(&rsink->rs_ts.at_rec_queue);
	M0_UT_ASSERT(rec_count == 3);

	/*
	 * Test 13: Call rpcsink_get_item()
	 *          with transient store has more records than can fill
	 *          in the remaining space but still it returned no record
	 *          since transient store not having record which can fit
	 *          in remaining space.
	 */

	item[2] = rpcsink_get_item(src, reclen[3]/2);
	M0_UT_ASSERT(item[2] == NULL);

	submitted = rpcsink_trans_rec_tlist_length(&rsink->rs_rpc_submitted);
	M0_UT_ASSERT(submitted == 3);

	rec_count = rec_queue_tlist_length(&rsink->rs_ts.at_rec_queue);
	M0_UT_ASSERT(rec_count == 3);


	/*
	 * Test 14: Call rpcsink_sent_item() with send failed.
	 *          Expected action : Should restore addb records in RPC sink.
	 */
	item[0]->ri_sm.sm_rc = -1;
	rpcsink_item_sent(item[0]);
	m0_fop_item_put(item[0]);

	submitted = rpcsink_trans_rec_tlist_length(&rsink->rs_rpc_submitted);
	M0_UT_ASSERT(submitted == 2);

	rec_count = rec_queue_tlist_length(&rsink->rs_ts.at_rec_queue);
	M0_UT_ASSERT(rec_count == 4);

	/*
	 * Test 15: Call rpcsink_sent_item() with send succeed.
	 *          Expected action : Should free addb records from RPC sink.
	 */
	item[1]->ri_sm.sm_rc = 0;
	rpcsink_item_sent(item[1]);
	m0_fop_item_put(item[1]);

	submitted = rpcsink_trans_rec_tlist_length(&rsink->rs_rpc_submitted);
	M0_UT_ASSERT(submitted == 0);

	rec_count = rec_queue_tlist_length(&rsink->rs_ts.at_rec_queue);
	M0_UT_ASSERT(rec_count == 4);

	/*
         * Submit remaining records.
         */
	item[3] = rpcsink_get_item(src,
				   reclen[0]+reclen[3]+reclen[4]+reclen[5]);
	M0_UT_ASSERT(item[3] != NULL);

	fop_rec_count = addb_ut_rpcsink_fop_records_count(item[3]);
	M0_UT_ASSERT(fop_rec_count == 4);

	submitted = rpcsink_trans_rec_tlist_length(&rsink->rs_rpc_submitted);
	M0_UT_ASSERT(submitted == 4);

	rec_count = rec_queue_tlist_length(&rsink->rs_ts.at_rec_queue);
	M0_UT_ASSERT(rec_count == 0);

	/*
	 * Successfully sent item[3]
	 */
	item[3]->ri_sm.sm_rc = 0;
	rpcsink_item_sent(item[3]);
	m0_fop_item_put(item[3]);

	submitted = rpcsink_trans_rec_tlist_length(&rsink->rs_rpc_submitted);
	M0_UT_ASSERT(submitted == 0);

	rec_count = rec_queue_tlist_length(&rsink->rs_ts.at_rec_queue);
	M0_UT_ASSERT(rec_count == 0);

	destroy_fake_rpcsink_item_source(src);
	m0_addb_mc_fini(&mc);

	addb_ct_tlist_del(&m0__addb_ut_ct0);
	m0__addb_ut_ct0.act_magic = 0;
	addb_rt_tlist_del(&m0__addb_ut_rt_dp1);
	m0__addb_ut_rt_dp1.art_magic = 0;
}

#ifndef __KERNEL__
static void addb_ut_rpcsink_shutdown_test(void)
{
	struct rpcsink            *rsink;
	struct m0_addb_rec_type   *dp = &m0__addb_ut_rt_dp1;
	struct m0_addb_ctx        *cv[4] = { NULL, &m0_addb_proc_ctx,
					     &m0_addb_node_ctx, NULL };
	int                        rc;
	int                        i;
	uint32_t		   rec_count;

	start_rpc_client_and_server();
	m0_addb_mc_init(&mc);

	rc = m0_addb_mc_configure_rpc_sink(&mc, &cctx.rcx_rpc_machine, NULL,
					   ADDB_UT_RPCSINK_TS_INIT_PAGES,
					   ADDB_UT_RPCSINK_TS_MAX_PAGES,
					   ADDB_UT_RPCSINK_TS_PAGE_SIZE);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_addb_mc_has_rpc_sink(&mc));

	rsink = rpcsink_from_mc(&mc);
	M0_UT_ASSERT(rsink != NULL);
	M0_UT_ASSERT(rsink->rs_ts.at_pages.ov_vec.v_nr ==
		     ADDB_UT_RPCSINK_TS_INIT_PAGES);
	M0_UT_ASSERT(rsink->rs_ts.at_max_pages ==
		     ADDB_UT_RPCSINK_TS_MAX_PAGES);
	M0_UT_ASSERT(rsink->rs_ts.at_page_size ==
		     ADDB_UT_RPCSINK_TS_PAGE_SIZE);

	rec_count = rec_queue_tlist_length(&rsink->rs_ts.at_rec_queue);
	M0_UT_ASSERT(rec_count == 0);

	/* Post some of ADDB records to populate transient store */
	m0_addb_mc_configure_pt_evmgr(&mc);

	m0__addb_ut_ct0.act_magic = 0;
	m0__addb_ut_ct0.act_id = addb_ct_max_id + 1;
	m0_addb_ctx_type_register(&m0__addb_ut_ct0);
	dp->art_magic = 0;
	dp->art_id = addb_rt_max_id + 1;

	m0_addb_rec_type_register(dp);
	M0_ADDB_CTX_INIT(&mc, &ctx, &m0__addb_ut_ct0, &m0_addb_proc_ctx);
	cv[0] = &ctx;

	for (i = 0;i < ADDB_UT_RPCSINK_LOOP_COUNTER; ++i)
		M0_ADDB_POST(&mc, dp, cv, i);

	m0_addb_mc_fini(&mc);
	stop_rpc_client_and_server();

	addb_ct_tlist_del(&m0__addb_ut_ct0);
	m0__addb_ut_ct0.act_magic = 0;
	addb_rt_tlist_del(&m0__addb_ut_rt_dp1);
	m0__addb_ut_rt_dp1.art_magic = 0;
}
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
