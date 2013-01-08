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
 * Original creation date: 10/19/2012
 */

/* This file is designed to be included by addb/ut/addb_ut.c */

/*
 ****************************************************************************
 * Test to configure a passthrough event manager
 ****************************************************************************
 */
static void addb_ut_evmgr_pt_config_test(void)
{
	struct m0_addb_mc mc = { 0 };
	struct m0_addb_mc_evmgr *evmgr;
	struct addb_pt_evmgr *pt;

	addb_ut_mc_reset();
	m0_addb_mc_init(&mc);
	addb_ut_mc_configure_recsink(&mc);
	m0_addb_mc_configure_pt_evmgr(&mc);

	evmgr = mc.am_evmgr;
	M0_UT_ASSERT(evmgr != NULL);
	M0_UT_ASSERT(evmgr->evm_magic == M0_ADDB_PT_EVMGR_MAGIC);
	M0_UT_ASSERT(evmgr->evm_put == addb_pt_evmgr_put);
	M0_UT_ASSERT(evmgr->evm_get == addb_pt_evmgr_get);
	M0_UT_ASSERT(evmgr->evm_rec_alloc == mc.am_sink->rs_rec_alloc);
	M0_UT_ASSERT(evmgr->evm_post == mc.am_sink->rs_save);
	M0_UT_ASSERT(evmgr->evm_copy == NULL);
	M0_UT_ASSERT(!evmgr->evm_post_awkward);

	pt = container_of(evmgr, struct addb_pt_evmgr, ape_evmgr);
	M0_UT_ASSERT(m0_atomic64_get(&pt->ape_ref.ref_cnt) == 1);

	/* fini */
	m0_addb_mc_fini(&mc);
}

/*
 ****************************************************************************
 * Posting test with a passthrough event manager
 ****************************************************************************
 */
struct addb_ut_evmgr_pt_thread_arg {
	struct m0_thread         t;
	int                      n;
	struct m0_addb_mc       *mc;
	struct m0_addb_ctx       ctx;
	struct m0_addb_rec_type *ex;
	size_t                   ex_reclen;
	struct m0_addb_rec_type *dp;
	size_t                   dp_reclen;
	struct m0_addb_rec_type *cntr;
	size_t                   cntr_reclen;
	size_t                   seq_reclen;
	struct m0_semaphore     *sem;
};
static struct addb_ut_evmgr_pt_thread_arg addb_ut_evmgr_pt_ta[10];

enum {
	ADDB_UT_EVMGR_PT_SEQ_MULT = 8
};

/* thread body */
static void addb_ut_evmgr_pt_thread(struct addb_ut_evmgr_pt_thread_arg
					 *ta)
{
	struct m0_addb_uint64_seq  seq;
	struct m0_addb_counter    *cntr;
	uint64_t                  *cntrp;
	int                        i;
	int                        rc;
	/*
	 * Use an explicit context vector variable instead of the compound
	 * literal macro as the compiler complains because the stack
	 * frame depth limit in the kernel (2048) gets exceeded in this sub at
	 * some point!
	 */
	struct m0_addb_ctx *cv[4] = {
		NULL, &m0_addb_proc_ctx, &m0_addb_node_ctx, NULL
	};

	/* init our context and CV */
	M0_ADDB_CTX_INIT(ta->mc, &ta->ctx, &m0__addb_ut_ct0, &m0_addb_proc_ctx);
	cv[0] = &ta->ctx; /* used to recover ta in the interception callback */

	/* init our sequence */
	seq.au64s_nr = ta->n * ADDB_UT_EVMGR_PT_SEQ_MULT;
	M0_ALLOC_ARR(seq.au64s_data, seq.au64s_nr);
	M0_ASSERT(ergo(seq.au64s_nr > 0, seq.au64s_data != NULL));
	for (i = 0; i < seq.au64s_nr; ++i)
		seq.au64s_data[i] = ta->n * i;

	M0_ALLOC_PTR(cntr);
	M0_ASSERT(cntr != NULL);
	rc = m0_addb_counter_init(cntr, ta->cntr);
	M0_ASSERT(rc == 0);
	cntrp = (uint64_t *)cntr->acn_data;
#define CNTR_DATA_SIZE sizeof(struct m0_addb_counter_data) / \
		       sizeof(uint64_t)
	for(i = 0; i < CNTR_DATA_SIZE + ta->n; i++)
		cntrp[i] = i * 10 + 10;
#undef CNTR_DATA_SIZE

	/* synchronize posts */
	m0_semaphore_down(ta->sem);

	switch (ta->n) {
	case 0:
		M0_ADDB_POST(ta->mc, ta->ex, cv);
		M0_ADDB_POST(ta->mc, ta->dp, cv);
		break;
	case 1:
		M0_ADDB_POST(ta->mc, ta->ex, cv, 1);
		M0_ADDB_POST(ta->mc, ta->dp, cv, 1);
		break;
	case 2:
		M0_ADDB_POST(ta->mc, ta->ex, cv, 1, 2);
		M0_ADDB_POST(ta->mc, ta->dp, cv, 2, 1);
		break;
	case 3:
		M0_ADDB_POST(ta->mc, ta->ex, cv, 1, 2, 3);
		M0_ADDB_POST(ta->mc, ta->dp, cv, 3, 2, 1);
		break;
	case 4:
		M0_ADDB_POST(ta->mc, ta->ex, cv, 1, 2, 3, 4);
		M0_ADDB_POST(ta->mc, ta->dp, cv, 4, 3, 2, 1);
		break;
	case 5:
		M0_ADDB_POST(ta->mc, ta->ex, cv, 1, 2, 3, 4, 5);
		M0_ADDB_POST(ta->mc, ta->dp, cv, 5, 4, 3, 2, 1);
		break;
	case 6:
		M0_ADDB_POST(ta->mc, ta->ex, cv, 1, 2, 3, 4, 5, 6);
		M0_ADDB_POST(ta->mc, ta->dp, cv, 6, 5, 4, 3, 2, 1);
		break;
	case 7:
		M0_ADDB_POST(ta->mc, ta->ex, cv, 1, 2, 3, 4, 5, 6, 7);
		M0_ADDB_POST(ta->mc, ta->dp, cv, 7, 6, 5, 4, 3, 2, 1);
		break;
	case 8:
		M0_ADDB_POST(ta->mc, ta->ex, cv, 1, 2, 3, 4, 5, 6, 7, 8);
		M0_ADDB_POST(ta->mc, ta->dp, cv, 8, 7, 6, 5, 4, 3, 2, 1);
		break;
	case 9:
		M0_ADDB_POST(ta->mc, ta->ex, cv, 1, 2, 3, 4, 5, 6, 7, 8, 9);
		M0_ADDB_POST(ta->mc, ta->dp, cv, 9, 8, 7, 6, 5, 4, 3, 2, 1);
		break;
	default:
		M0_ASSERT(ta->n < 9);
	}
	M0_ADDB_POST_CNTR(ta->mc, cv, cntr);
	M0_ADDB_POST_SEQ(ta->mc, &m0__addb_ut_rt_seq, cv, &seq);

	m0_addb_ctx_fini(&ta->ctx);
	m0_free(seq.au64s_data);
	m0_addb_counter_fini(cntr);
	m0_free(cntr);
	return;
}

/* fake sink intercept callback */
static int addb_ut_evmgr_pt_cb_num;
static void addb_ut_evmgr_pt_cb(const struct m0_addb_rec *rec)
{
	uint32_t ctxpathlen = 0;
	uint64_t ctxpath[4];
	uint32_t fields_nr = 0;
	uint64_t *fields;
	struct addb_ut_evmgr_pt_thread_arg *ta;
	int i;
	size_t *reclen;

	if (addb_rec_post_ut_data.brt > M0_ADDB_BRT_SEQ)
		return;

	/* We explicitly embedded a context in the thread argument so we
	 * can recover the thread argument structure here.
	 */
	ta = container_of(addb_rec_post_ut_data.cv[0],
			  struct addb_ut_evmgr_pt_thread_arg, ctx);
	M0_ASSERT(ta->n < ARRAY_SIZE(addb_ut_evmgr_pt_ta));

	/* assemble the data to validate */
	ctxpathlen = 4;
	ctxpath[0] = m0_node_uuid.u_hi;
	ctxpath[1] = m0_node_uuid.u_lo;
	ctxpath[2] = m0_addb_proc_ctx.ac_id;
	ctxpath[3] = ta->ctx.ac_id;

	fields_nr = addb_rec_post_ut_data.fields_nr;
	fields    = addb_rec_post_ut_data.fields;

	switch (addb_rec_post_ut_data.brt) {
	case M0_ADDB_BRT_EX:
		M0_ASSERT(fields_nr == ta->n);
		reclen = &ta->ex_reclen;
		break;
	case M0_ADDB_BRT_DP:
		M0_ASSERT(fields_nr == ta->n);
		reclen = &ta->dp_reclen;
		break;
	case M0_ADDB_BRT_CNTR:
		reclen = &ta->cntr_reclen;
		break;
	case M0_ADDB_BRT_SEQ:
		reclen = &ta->seq_reclen;
		break;
	default:
		return;
	}

	/* validate */
	M0_UT_ASSERT(rec->ar_rid == addb_rec_post_ut_data.rid);
	M0_UT_ASSERT(addb_rec_post_ut_data.cv_nr == 3);
	M0_UT_ASSERT(rec->ar_ctxids.acis_nr == 3);
	M0_UT_ASSERT(rec->ar_ctxids.acis_data != NULL);
	for (i = 0; i < rec->ar_ctxids.acis_nr; ++i, --ctxpathlen) {
		int j;
		struct m0_addb_uint64_seq *u64s =
			&rec->ar_ctxids.acis_data[i];
		M0_UT_ASSERT(u64s->au64s_nr == ctxpathlen);
		for (j = 0; j < ctxpathlen; ++j)
			M0_UT_ASSERT(u64s->au64s_data[j] == ctxpath[j]);
	}

	M0_UT_ASSERT(rec->ar_data.au64s_nr == fields_nr);
	for (i = 0; i < fields_nr; ++i)
		M0_UT_ASSERT(rec->ar_data.au64s_data[i] == fields[i]);

	M0_UT_ASSERT(addb_ut_mc_rs_rec_len == addb_rec_post_ut_data.reclen);
	*reclen = addb_ut_mc_rs_rec_len;

	++addb_ut_evmgr_pt_cb_num;

	addb_rec_post_ut_data.brt = M0_ADDB_BRT_NR; /* reset */
}

/* fake sink intercept callback for standard exceptions */
static void addb_ut_evmgr_pt_cb2(const struct m0_addb_rec *rec)
{
	uint32_t cv_nr;
	uint32_t ctxpathlen;
	uint64_t ctxpath[3];
	uint32_t fields_nr = 0;
	uint64_t fields[2];
	int i;

	if (addb_rec_post_ut_data.brt != M0_ADDB_BRT_EX)
		return;

	ctxpath[0] = m0_node_uuid.u_hi;
	ctxpath[1] = m0_node_uuid.u_lo;
	ctxpath[2] = m0_addb_proc_ctx.ac_id;

	if (addb_ut_evmgr_pt_cb_num % 2 == 0) {
		cv_nr = 1;
	} else {
		cv_nr = 2;
	}

	switch (addb_rec_post_ut_data.brt) {
	case M0_ADDB_RECID_FUNC_FAIL:
		fields[fields_nr++] = -1 * (addb_ut_evmgr_pt_cb_num + 1);
		fields[fields_nr++] = addb_ut_evmgr_pt_cb_num + 1;
		break;
	case M0_ADDB_RECID_OOM:
		fields[fields_nr++] = addb_ut_evmgr_pt_cb_num + 1;
		break;
	default:
		goto last;
	}

	/* validate */
	M0_UT_ASSERT(rec->ar_rid == addb_rec_post_ut_data.rid);
	M0_UT_ASSERT(addb_rec_post_ut_data.cv_nr == cv_nr);
	M0_UT_ASSERT(rec->ar_ctxids.acis_nr == cv_nr);
	M0_UT_ASSERT(rec->ar_ctxids.acis_data != NULL);
	for (i = 0, ctxpathlen = 2;
	     i < rec->ar_ctxids.acis_nr; ++i, ++ctxpathlen) {
		int j;
		struct m0_addb_uint64_seq *u64s =
			&rec->ar_ctxids.acis_data[i];
		M0_UT_ASSERT(u64s->au64s_nr == ctxpathlen);
		for (j = 0; j < ctxpathlen; ++j)
			M0_UT_ASSERT(u64s->au64s_data[j] == ctxpath[j]);
	}
	M0_UT_ASSERT(addb_rec_post_ut_data.fields_nr == fields_nr);
	M0_UT_ASSERT(rec->ar_data.au64s_nr == fields_nr);
	for (i = 0; i < fields_nr; ++i)
		M0_UT_ASSERT(rec->ar_data.au64s_data[i] == fields[i]);
	M0_UT_ASSERT(addb_ut_mc_rs_rec_len == addb_rec_post_ut_data.reclen);

 last:
	++addb_ut_evmgr_pt_cb_num;
	addb_rec_post_ut_data.brt = M0_ADDB_BRT_NR; /* reset */
}

static void addb_ut_evmgr_pt_post_test(void)
{
	struct m0_semaphore sem;
	struct m0_addb_mc mc = { 0 };
	int i;
	int rc;
	int *ip;

	/* init */
	rc = m0_semaphore_init(&sem, 0);
	M0_ASSERT(rc == 0);
	addb_ut_mc_reset();
	m0_addb_mc_init(&mc);
	addb_ut_mc_configure_recsink(&mc);
	m0_addb_mc_configure_pt_evmgr(&mc);
	M0_UT_ASSERT(m0_addb_mc_is_fully_configured(&mc));
	addb_ut_mc_rs_save_cb = addb_ut_evmgr_pt_cb; /* intercept saves */

	/*
	 * Temporarily register the fake record types from addb_ut_md.c,
	 * and save to thread args.
	 * Also create one fake context type, mainly to recover the
	 * thread argument in the fake sink callback.
	 */
	m0__addb_ut_ct0.act_magic = 0;
	m0__addb_ut_ct0.act_id = addb_ct_max_id + 1;
	m0_addb_ctx_type_register(&m0__addb_ut_ct0);
	m0__addb_ut_rt_seq.art_magic = 0;
	m0__addb_ut_rt_seq.art_id = addb_rt_max_id + 1;
	m0_addb_rec_type_register(&m0__addb_ut_rt_seq);

#undef RT_REG
#define RT_REG(n)						\
	m0__addb_ut_rt_ex##n.art_id = addb_rt_max_id + 1;	\
	m0_addb_rec_type_register(&m0__addb_ut_rt_ex##n);	\
	addb_ut_evmgr_pt_ta[n].ex = &m0__addb_ut_rt_ex##n;	\
	m0__addb_ut_rt_dp##n.art_id = addb_rt_max_id + 1;	\
	m0_addb_rec_type_register(&m0__addb_ut_rt_dp##n);	\
	addb_ut_evmgr_pt_ta[n].dp = &m0__addb_ut_rt_dp##n;	\
	m0__addb_ut_rt_cntr##n.art_id = addb_rt_max_id + 1;	\
	m0_addb_rec_type_register(&m0__addb_ut_rt_cntr##n);	\
	addb_ut_evmgr_pt_ta[n].cntr = &m0__addb_ut_rt_cntr##n

	RT_REG(0);
	RT_REG(1);
	RT_REG(2);
	RT_REG(3);
	RT_REG(4);
	RT_REG(5);
	RT_REG(6);
	RT_REG(7);
	RT_REG(8);
	RT_REG(9);

#undef RT_REG

	/*
	 * Test
	 * Post using the initialized but unconfigured global machine.
	 * It won't work, but it won't fail either.
	 */
	M0_ASSERT(!m0_addb_mc_is_configured(&m0_addb_gmc));
	M0_ADDB_POST(&m0_addb_gmc, &m0__addb_ut_rt_ex0,
		     M0_ADDB_CTX_VEC(&m0_addb_node_ctx));

	/*
	 * Test
	 * Post ADDB records on background threads to validate concurrency.
	 */
	for (i = 0; i < ARRAY_SIZE(addb_ut_evmgr_pt_ta); ++i) {
		struct addb_ut_evmgr_pt_thread_arg *ta =
			&addb_ut_evmgr_pt_ta[i];
		ta->n = i;
		ta->mc = &mc;
		ta->sem = &sem;
		M0_SET0(&ta->t);
		M0_ASSERT(ta->ex != NULL); /* set in RT_REG */
		M0_ASSERT(ta->dp != NULL); /* set in RT_REG */
		M0_ASSERT(ta->cntr != NULL); /* set in RT_REG */
		M0_ASSERT(ta->ex_reclen == 0);
		M0_ASSERT(ta->dp_reclen == 0);
		M0_ASSERT(ta->cntr_reclen == 0);
		M0_ASSERT(ta->seq_reclen == 0);
		rc = M0_THREAD_INIT(&ta->t,
				    struct addb_ut_evmgr_pt_thread_arg *,
				    NULL, &addb_ut_evmgr_pt_thread, ta,
				    "addb_ut_evmgr%d", i);
		M0_ASSERT(rc == 0);
	}
	for (i = 0; i < ARRAY_SIZE(addb_ut_evmgr_pt_ta); ++i)
		m0_semaphore_up(&sem); /* unblock threads */
	for (i = 0; i < ARRAY_SIZE(addb_ut_evmgr_pt_ta); ++i) {
		struct addb_ut_evmgr_pt_thread_arg *ta =
			&addb_ut_evmgr_pt_ta[i];
		m0_thread_join(&ta->t);
		m0_thread_fini(&ta->t);
	}

	M0_UT_ASSERT(addb_ut_evmgr_pt_cb_num == 40);

	/* validate record lengths are montonically increasing */
	for (i = 0; i < ARRAY_SIZE(addb_ut_evmgr_pt_ta); ++i) {
		struct addb_ut_evmgr_pt_thread_arg *ta =
			&addb_ut_evmgr_pt_ta[i];
		if (i == 0) {
			M0_UT_ASSERT(ta->ex_reclen > 0);
			M0_UT_ASSERT(ta->dp_reclen > 0);
			M0_UT_ASSERT(ta->seq_reclen > 0);
			M0_UT_ASSERT(ta->cntr_reclen > 0);
		} else {
			M0_UT_ASSERT(ta->ex_reclen >=
				     addb_ut_evmgr_pt_ta[i-1].ex_reclen +
				     sizeof(uint64_t));
			M0_UT_ASSERT(ta->dp_reclen >=
				     addb_ut_evmgr_pt_ta[i-1].dp_reclen +
				     sizeof(uint64_t));
			M0_UT_ASSERT(ta->seq_reclen >=
				     addb_ut_evmgr_pt_ta[i-1].seq_reclen +
				     ADDB_UT_EVMGR_PT_SEQ_MULT *
				     sizeof(uint64_t));
			M0_UT_ASSERT(ta->cntr_reclen >=
				     addb_ut_evmgr_pt_ta[i-1].cntr_reclen +
				     sizeof(uint64_t));
		}
	}

	/*
	 * TEST
	 * Post standard exceptions using ADDB macros.
	 */
	addb_ut_mc_rs_save_cb = addb_ut_evmgr_pt_cb2; /* intercept saves */
	addb_ut_evmgr_pt_cb_num = 0;
	M0_ADDB_FUNC_FAIL(&mc, 1, -1, &m0_addb_node_ctx);
	M0_ADDB_FUNC_FAIL(&mc, 2, -2, &m0_addb_node_ctx, &m0_addb_proc_ctx);
	M0_ADDB_OOM(&mc, 3, &m0_addb_node_ctx);
	M0_ADDB_OOM(&mc, 4, &m0_addb_node_ctx, &m0_addb_proc_ctx);

	/* use fault injection to test the offical allocation macros */
	m0_fi_enable_once("m0_alloc", "fail_allocation");
	M0_ALLOC_ADDB(ip, sizeof(*ip), &mc, 5, &m0_addb_node_ctx);
	m0_fi_enable_once("m0_alloc", "fail_allocation");
	M0_ALLOC_PTR_ADDB(ip, &mc, 6, &m0_addb_node_ctx, &m0_addb_proc_ctx);
	m0_fi_enable_once("m0_alloc", "fail_allocation");
	M0_ALLOC_ARR_ADDB(ip, 99, &mc, 7, &m0_addb_node_ctx);

	M0_UT_ASSERT(addb_ut_evmgr_pt_cb_num == 7);

	/* fini */
	addb_ut_mc_rs_save_cb = NULL;
	m0_addb_mc_fini(&mc);
	/* no unregister APIs, so delete record/ctx types from the hash table */
	for (i = 0; i < 10; ++i) {
		addb_rt_tlist_del(addb_ut_evmgr_pt_ta[i].ex);
		M0_UT_ASSERT(m0_addb_rec_type_lookup(addb_ut_evmgr_pt_ta[i]
						     .ex->art_id) == NULL);
		addb_rt_tlist_del(addb_ut_evmgr_pt_ta[i].dp);
		M0_UT_ASSERT(m0_addb_rec_type_lookup(addb_ut_evmgr_pt_ta[i]
						     .dp->art_id) == NULL);
		addb_rt_tlist_del(addb_ut_evmgr_pt_ta[i].cntr);
		M0_UT_ASSERT(m0_addb_rec_type_lookup(addb_ut_evmgr_pt_ta[i]
						     .cntr->art_id) == NULL);
		addb_ut_evmgr_pt_ta[i].cntr->art_magic = 0;
	}
	addb_rt_tlist_del(&m0__addb_ut_rt_seq);
	addb_ct_tlist_del(&m0__addb_ut_ct0);
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
