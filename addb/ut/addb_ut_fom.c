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
 * Original author: Rohan Puri <rohan_puri@xyratex.com>
 * Original creation date: 04/10/2013
 */

extern const struct m0_tl_descr rec_queue_tl;

enum {
	UT_ADDB_FOM_TS_INIT_PAGES = 16,
	UT_ADDB_FOM_TS_MAX_PAGES  = 36,
	UT_ADDB_FOM_TS_PAGE_SIZE  = 4096, /* bytes */
	UT_ADDB_FOM_REC_NR = 100,
};

static const struct m0_stob_id addb_ut_fom_stobid = {
	.si_bits = { .u_hi = 0, .u_lo = 1 }
};

struct addb_ut_fom_data {
	struct m0_stob              *aufd_stob;
	struct m0_addb_segment_iter *aufd_iter;
};

uint32_t               reclen[6];
struct m0_rpc_item    *item[4];
struct m0_addb_mc      mc;
struct m0_rpc_machine  rm;
struct m0_addb_ctx     ctx;

/*
 * Global array of pointers to addb records that would be posted via rpcsink
 */
struct m0_addb_rec *ut_addb_rec_arr[UT_ADDB_FOM_REC_NR];

static struct m0_addb_ts_rec_header *
addb_ut_get_last_posted_addb_ts_rec_header(struct rpcsink *rsink)
{
	struct m0_addb_ts_rec_header *header;

	header = rec_queue_tlist_tail(&rsink->rs_ts.at_rec_queue);
	return header;
}

int addb_ut_fom_compare_recs(struct m0_addb_rec *rec1,
			      struct m0_addb_rec *rec2)
{
	return m0_xcode_cmp(&M0_XCODE_OBJ(m0_addb_rec_xc, rec1),
			    &M0_XCODE_OBJ(m0_addb_rec_xc, rec2));
}

static void addb_ut_fom_verify_addb_rec_on_stob(struct m0_addb_rec      *rec,
						struct addb_ut_fom_data *ut_d)
{
	struct m0_addb_cursor cur;
	struct m0_addb_rec   *rec_from_stob;
	int                   rc;
	int                   not_found = 1;

	rc = m0_addb_cursor_init(&cur, ut_d->aufd_iter, M0_ADDB_CURSOR_REC_ANY);
	M0_UT_ASSERT(rc == 0);

	while (1) {
		rc = m0_addb_cursor_next(&cur, &rec_from_stob);
		if (rc == -ENODATA)
			break;
		not_found = addb_ut_fom_compare_recs(rec, rec_from_stob);
		if (not_found == 0)
			break;
	}
	M0_UT_ASSERT(not_found == 0);
	m0_addb_cursor_fini(&cur);
}

static struct m0_addb_rec *
fill_global_array_with_last_posted_addb_rec(struct rpcsink *rsink)
{
	struct m0_addb_ts_rec_header *rec_header;
	struct m0_bufvec_cursor       cur;
	struct m0_addb_ts_rec        *ts_rec;
	struct m0_addb_rec           *rec;
	struct m0_addb_rec           *out_rec;
	struct m0_bufvec              bv;
	uint32_t                      rec_size;
	int                           rc;

	rec_header = addb_ut_get_last_posted_addb_ts_rec_header(rsink);
	M0_UT_ASSERT(rec_header != NULL);
	ts_rec = bob_of(rec_header, struct m0_addb_ts_rec, atr_header,
			&addb_ts_rec_bob);
	M0_UT_ASSERT(ts_rec != NULL);
	rec = (struct m0_addb_rec *)&ts_rec->atr_data[0];
	rec_size = addb_rec_payload_size(rec);
	out_rec = m0_alloc(rec_size);
	M0_UT_ASSERT(out_rec != NULL);
	rc = m0_bufvec_alloc(&bv, 1, rec_size + 1);
	M0_UT_ASSERT(rc == 0);
	m0_bufvec_cursor_init(&cur, &bv);

	rc = addb_rec_seq_enc(rec, &cur, m0_addb_rec_xc);
	M0_UT_ASSERT(rc == 0);
	m0_bufvec_cursor_init(&cur, &bv);
	rc = m0_xcode_encdec(&M0_XCODE_OBJ(m0_addb_rec_xc, out_rec), &cur,
			     M0_XCODE_DECODE);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(out_rec != NULL);
	m0_bufvec_free(&bv);

	return out_rec;
}

static char *addb_server_argv[] = {
	"rpclib_ut", "-r", "-p", "-T", "linux", "-D", SERVER_DB_NAME,
	"-S", SERVER_STOB_NAME, "-A", SERVER_ADDB_STOB_NAME,
	"-e", SERVER_ENDPOINT, "-s", "ds1", "-s", "ds2", "-s", "addb", "-w",
	"10"
};

static void addb_ut_fom_test(void)
{
	struct m0_addb_rec_type   *dp = &m0__addb_ut_rt_dp1;
	struct m0_addb_ctx        *cv[4] = { NULL, &m0_addb_proc_ctx,
					     &m0_addb_node_ctx, NULL };
	struct addb_ut_fom_data    ut_data;
	struct m0_stob_domain     *dom;
	struct rpcsink            *rsink;
	uint32_t		   rec_count;
	int                        rc;
	int                        i;

	sctx.rsx_argv = addb_server_argv;
	start_rpc_client_and_server();
	m0_addb_mc_init(&mc);

	rc = m0_addb_mc_configure_rpc_sink(&mc, &cctx.rcx_rpc_machine, NULL,
					   UT_ADDB_FOM_TS_INIT_PAGES,
					   UT_ADDB_FOM_TS_MAX_PAGES,
					   UT_ADDB_FOM_TS_PAGE_SIZE);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_addb_mc_has_rpc_sink(&mc));

	rsink = rpcsink_from_mc(&mc);
	M0_UT_ASSERT(rsink != NULL);
	M0_UT_ASSERT(rsink->rs_ts.at_pages.ov_vec.v_nr ==
		     UT_ADDB_FOM_TS_INIT_PAGES);
	M0_UT_ASSERT(rsink->rs_ts.at_max_pages ==
		     UT_ADDB_FOM_TS_MAX_PAGES);
	M0_UT_ASSERT(rsink->rs_ts.at_page_size ==
		     UT_ADDB_FOM_TS_PAGE_SIZE);

	rec_count = rec_queue_tlist_length(&rsink->rs_ts.at_rec_queue);
	M0_UT_ASSERT(rec_count == 0);

	m0_addb_mc_configure_pt_evmgr(&mc);
	m0__addb_ut_ct0.act_magic = 0;
	m0__addb_ut_ct0.act_id = addb_ct_max_id + 1;
	m0_addb_ctx_type_register(&m0__addb_ut_ct0);
	dp->art_magic = 0;
	dp->art_id = addb_rt_max_id + 1;
	m0_addb_rec_type_register(dp);
	M0_ADDB_CTX_INIT(&mc, &ctx, &m0__addb_ut_ct0, &m0_addb_proc_ctx);
	ut_addb_rec_arr[0] = fill_global_array_with_last_posted_addb_rec(rsink);
	cv[0] = &ctx;

	for (i = 1; i < UT_ADDB_FOM_REC_NR; ++i) {
		M0_ADDB_POST(&mc, dp, cv, i);
		ut_addb_rec_arr[i] =
			fill_global_array_with_last_posted_addb_rec(rsink);
	}

	m0_addb_mc_fini(&mc);
	stop_rpc_client_and_server();

	M0_SET0(&ut_data);

	ut_data.aufd_stob = addb_ut_retrieval_stob_setup(SERVER_ADDB_STOB_NAME,
							 &addb_ut_fom_stobid);
	M0_UT_ASSERT(ut_data.aufd_stob != NULL);
	dom = ut_data.aufd_stob->so_domain;
	M0_UT_ASSERT(dom != NULL);

	for (i = 0; i < UT_ADDB_FOM_REC_NR; ++i) {
		rc = m0_addb_stob_iter_alloc(&ut_data.aufd_iter,
					     ut_data.aufd_stob);
		M0_UT_ASSERT(ut_data.aufd_iter != NULL);
		addb_ut_fom_verify_addb_rec_on_stob(ut_addb_rec_arr[i],
						    &ut_data);
		m0_addb_segment_iter_free(ut_data.aufd_iter);
	}

	m0_stob_put(ut_data.aufd_stob);
	dom->sd_ops->sdo_fini(dom);
	for (i = 0; i < UT_ADDB_FOM_REC_NR; ++i)
		m0_xcode_free(&M0_XCODE_OBJ(m0_addb_rec_xc,
					    ut_addb_rec_arr[i]));
	addb_ct_tlist_del(&m0__addb_ut_ct0);
	m0__addb_ut_ct0.act_magic = 0;
	addb_rt_tlist_del(&m0__addb_ut_rt_dp1);
	m0__addb_ut_rt_dp1.art_magic = 0;
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
