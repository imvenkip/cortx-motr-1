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
 * Original creation date: 10/09/2012
 */

/* This file is designed to be included by addb/ut/addb_ut.c */

/*
 ****************************************************************************
 * Test to validate that global contexts are initialized.
 ****************************************************************************
 */
static void addb_ut_ctx_global_init_test(void)
{
	M0_UT_ASSERT(addb_node_root_ctx.ac_magic  == M0_ADDB_CTX_MAGIC);
	M0_UT_ASSERT(addb_node_root_ctx.ac_type   == &m0_addb_ct_node_hi);
	M0_UT_ASSERT(addb_node_root_ctx.ac_id     == m0_node_uuid.u_hi);
	M0_UT_ASSERT(addb_node_root_ctx.ac_parent == NULL);
	M0_UT_ASSERT(addb_node_root_ctx.ac_imp_id == NULL);
	M0_UT_ASSERT(addb_node_root_ctx.ac_cntr   == 1);
	M0_UT_ASSERT(addb_node_root_ctx.ac_depth  == 1);

	M0_UT_ASSERT(m0_addb_node_ctx.ac_magic  == M0_ADDB_CTX_MAGIC);
	M0_UT_ASSERT(m0_addb_node_ctx.ac_type   == &m0_addb_ct_node_lo);
	M0_UT_ASSERT(m0_addb_node_ctx.ac_id     == m0_node_uuid.u_lo);
	M0_UT_ASSERT(m0_addb_node_ctx.ac_parent == &addb_node_root_ctx);
	M0_UT_ASSERT(m0_addb_node_ctx.ac_imp_id == NULL);
	M0_UT_ASSERT(m0_addb_node_ctx.ac_cntr   == 1);
	M0_UT_ASSERT(m0_addb_node_ctx.ac_depth  ==
		     addb_node_root_ctx.ac_depth + 1);

	M0_UT_ASSERT(m0_addb_proc_ctx.ac_magic  == M0_ADDB_CTX_MAGIC);
	M0_UT_ASSERT(m0_addb_proc_ctx.ac_id      > 0);
#ifdef __KERNEL__
	M0_UT_ASSERT(m0_addb_proc_ctx.ac_type   == &m0_addb_ct_kmod);
	M0_UT_ASSERT(m0_addb_proc_ctx.ac_id     == addb_init_time);
#else
	M0_UT_ASSERT(m0_addb_proc_ctx.ac_type   == &m0_addb_ct_process);
	{
		pid_t masked_pid = getpid() & ADDB_PROC_CTX_MASK;

		M0_UT_ASSERT((m0_addb_proc_ctx.ac_id & ADDB_PROC_CTX_MASK)
			     == masked_pid);
		M0_UT_ASSERT(m0_addb_proc_ctx.ac_id > masked_pid);
		M0_UT_ASSERT((m0_addb_proc_ctx.ac_id & ~ADDB_PROC_CTX_MASK)
			     == (addb_init_time & ~ADDB_PROC_CTX_MASK));
	}
#endif
	M0_UT_ASSERT(m0_addb_proc_ctx.ac_parent == &m0_addb_node_ctx);
	M0_UT_ASSERT(m0_addb_proc_ctx.ac_imp_id == NULL);
	/*
	 * Cannot test the value of the ac_cntr field because other modules
	 * have initialized themselves by now and created child contexts.
	 *	M0_UT_ASSERT(m0_addb_proc_ctx.ac_cntr == 0);
	 */
	M0_UT_ASSERT(m0_addb_proc_ctx.ac_depth  == m0_addb_node_ctx.ac_depth+1);
}

/*
 ****************************************************************************
 * Test for global context record definition post and for deferred
 * posts of contexts initialized with the unconfigured global machine.
 *
 * Luckily for this UT, the pending context record of "real" modules
 * have been flushed with other UT configurations of the global machine.
 ****************************************************************************
 */
static struct m0_addb_ctx addb_ut_def_ctx[10]; /* deferred contexts */
static int addb_ut_ctx_gpt_cb_num;
static void addb_ut_ctx_gpt_cb(const struct m0_addb_rec *rec)
{
	uint64_t rid = 0;
	uint32_t nf = 0;
	uint32_t ctxpathlen = 0;
	uint32_t ctxid = 0;
	uint64_t f[10];
	uint64_t ctxpath[3];
	int i;

	if (addb_ut_ctx_gpt_cb_num == 0) {
		/* node context (hi) */
		ctxid = M0_ADDB_CTXID_NODE_HI;
		ctxpathlen = 1;
		ctxpath[0] = m0_node_uuid.u_hi;
	} else if (addb_ut_ctx_gpt_cb_num == 1) {
		/* node context (lo) */
		ctxid = M0_ADDB_CTXID_NODE_LO;
		ctxpathlen = 2;
		ctxpath[0] = m0_node_uuid.u_hi;
		ctxpath[1] = m0_node_uuid.u_lo;
	} else if (addb_ut_ctx_gpt_cb_num == 2) {
		/* proc context */
		ctxpathlen = 3;
		ctxpath[0] = m0_node_uuid.u_hi;
		ctxpath[1] = m0_node_uuid.u_lo;
		ctxpath[2] = m0_addb_proc_ctx.ac_id;
		f[0] = (uint64_t)addb_init_time;
#ifdef __KERNEL__
		ctxid = M0_ADDB_CTXID_KMOD;
		nf = 1;
#else
		ctxid = M0_ADDB_CTXID_PROCESS;
		nf = 2;
		f[1] = (uint64_t)getpid();
#endif
	} else { /* deferred contexts */
		int j;

		M0_ASSERT(addb_ut_ctx_gpt_cb_num >= 3);
		i = addb_ut_ctx_gpt_cb_num - 3;
		M0_ASSERT(i < ARRAY_SIZE(addb_ut_def_ctx));
		ctxpathlen = 3;
		ctxpath[0] = m0_node_uuid.u_hi;
		ctxpath[1] = m0_node_uuid.u_lo;
		ctxpath[2] = addb_ut_def_ctx[i].ac_id;
		ctxid      = addb_ut_def_ctx[i].ac_type->act_id;
		nf         = i;
		for (j = 0; j < nf; ++j)
			f[j] = j + 1;
	}
	M0_ASSERT(nf <= ARRAY_SIZE(f));
	rid = m0_addb_rec_rid_make(M0_ADDB_BRT_CTXDEF, ctxid);
	M0_UT_ASSERT(rec->ar_rid == rid);
	M0_UT_ASSERT(m0_addb_rec_rid_to_brt(rid) == M0_ADDB_BRT_CTXDEF);
	M0_UT_ASSERT(m0_addb_rec_rid_to_id(rid) == ctxid);
	M0_UT_ASSERT(rec->ar_ctxids.acis_nr == 1);
	for (i = 0; i < ctxpathlen; ++i)
		M0_UT_ASSERT(rec->ar_ctxids.acis_data->au64s_data[i]
			     == ctxpath[i]);
	M0_UT_ASSERT(rec->ar_data.au64s_nr == nf);
	for (i = 0; i < nf; ++i)
		M0_UT_ASSERT(rec->ar_data.au64s_data[i] == f[i]);
	++addb_ut_ctx_gpt_cb_num;
}

static void addb_ut_ctx_global_post_test(void)
{
	struct m0_addb_mc          mc1 = { 0 };
	struct addb_ctx_def_cache *ce;
	int                        i;

	addb_ut_mc_reset();
	m0_addb_mc_init(&mc1);
	addb_ut_mc_configure_recsink(&mc1);
	addb_ut_mc_configure_pt_evmgr(&mc1);
	M0_UT_ASSERT(m0_addb_mc_is_fully_configured(&mc1));

	/*
	 * Initialize contexts with the unconfigured global ADDB machine, to
	 * test the deferred posting of context definition records.
	 * Temporarily register the fake UT contexts from addb_ut_md.c.
	 */
#undef CT_REG
#define CT_REG(n)					\
	m0__addb_ut_ct##n.act_id = addb_ct_max_id + 1;	\
	m0_addb_ctx_type_register(&m0__addb_ut_ct##n)

	CT_REG(0);
	CT_REG(1);
	CT_REG(2);
	CT_REG(3);
	CT_REG(4);
	CT_REG(5);
	CT_REG(6);
	CT_REG(7);
	CT_REG(8);
	CT_REG(9);

#undef CT_REG

	M0_UT_ASSERT(addb_cdc_tlist_is_empty(&addb_cdc));
	for (i = 0; i < ARRAY_SIZE(addb_ut_def_ctx); ++i) {
		M0_UT_ASSERT(addb_cdc_tlist_length(&addb_cdc) == i);
		switch (i) {
		case 0:	M0_ADDB_CTX_INIT(&m0_addb_gmc, &addb_ut_def_ctx[0],
					 &m0__addb_ut_ct0, &m0_addb_node_ctx);
			break;
		case 1:	M0_ADDB_CTX_INIT(&m0_addb_gmc, &addb_ut_def_ctx[1],
					 &m0__addb_ut_ct1, &m0_addb_node_ctx,
					 1);
			break;
		case 2:	M0_ADDB_CTX_INIT(&m0_addb_gmc, &addb_ut_def_ctx[2],
					 &m0__addb_ut_ct2, &m0_addb_node_ctx,
					 1, 2);
			break;
		case 3:	M0_ADDB_CTX_INIT(&m0_addb_gmc, &addb_ut_def_ctx[3],
					 &m0__addb_ut_ct3, &m0_addb_node_ctx,
					 1, 2, 3);
			break;
		case 4:	M0_ADDB_CTX_INIT(&m0_addb_gmc, &addb_ut_def_ctx[4],
					 &m0__addb_ut_ct4, &m0_addb_node_ctx,
					 1, 2, 3, 4);
			break;
		case 5:	M0_ADDB_CTX_INIT(&m0_addb_gmc, &addb_ut_def_ctx[5],
					 &m0__addb_ut_ct5, &m0_addb_node_ctx,
					 1, 2, 3, 4, 5);
			break;
		case 6:	M0_ADDB_CTX_INIT(&m0_addb_gmc, &addb_ut_def_ctx[6],
					 &m0__addb_ut_ct6, &m0_addb_node_ctx,
					 1, 2, 3, 4, 5, 6);
			break;
		case 7:	M0_ADDB_CTX_INIT(&m0_addb_gmc, &addb_ut_def_ctx[7],
					 &m0__addb_ut_ct7, &m0_addb_node_ctx,
					 1, 2, 3, 4, 5, 6, 7);
			break;
		case 8:	M0_ADDB_CTX_INIT(&m0_addb_gmc, &addb_ut_def_ctx[8],
					 &m0__addb_ut_ct8, &m0_addb_node_ctx,
					 1, 2, 3, 4, 5, 6, 7, 8);
			break;
		case 9:	M0_ADDB_CTX_INIT(&m0_addb_gmc, &addb_ut_def_ctx[9],
					 &m0__addb_ut_ct9, &m0_addb_node_ctx,
					 1, 2, 3, 4, 5, 6, 7, 8, 9);
			break;
		}
		M0_UT_ASSERT(addb_cdc_tlist_length(&addb_cdc) == i + 1);
	}

	/* check that they are cached in INIT order */
	i = 0;
	m0_tl_for(addb_cdc, &addb_cdc, ce) {
		int j;

		M0_UT_ASSERT(ce->cdc_ctx == &addb_ut_def_ctx[i]);
		for (j = 0; j < i; ++j)
			M0_UT_ASSERT(ce->cdc_fields[j] == j + 1);
		++i;
	} m0_tl_endfor;
	M0_UT_ASSERT(i == ARRAY_SIZE(addb_ut_def_ctx));

	/* test that a cached context gets un-cached when fini'd */
	{
		struct m0_addb_ctx ctx = { 0 };

		i = addb_cdc_tlist_length(&addb_cdc);
		M0_ADDB_CTX_INIT(&m0_addb_gmc, &ctx,
				 &m0__addb_ut_ct3, &m0_addb_node_ctx, 1, 2, 3);
		M0_UT_ASSERT(addb_cdc_tlist_length(&addb_cdc) == i + 1);
		ce = addb_cdc_tlist_tail(&addb_cdc);
		M0_ASSERT(ce != NULL);
		M0_UT_ASSERT(ce->cdc_ctx == &ctx);
		m0_addb_ctx_fini(&ctx);
		M0_UT_ASSERT(addb_cdc_tlist_length(&addb_cdc) == i);
		ce = addb_cdc_tlist_tail(&addb_cdc);
		M0_ASSERT(ce != NULL);
		M0_UT_ASSERT(ce->cdc_ctx != &ctx);
	}

	/*
	 * initialize the global machine as a dup of mc2 and ensure that
	 * posting of global context definitions and cached definitions
	 * was done.
	 */
	M0_UT_ASSERT(addb_ut_ctx_gpt_cb_num == 0);
	addb_ut_mc_rs_save_cb = addb_ut_ctx_gpt_cb;
	m0_addb_mc_dup(&mc1, &m0_addb_gmc);
	M0_UT_ASSERT(m0_addb_mc_is_fully_configured(&m0_addb_gmc));
	addb_ut_mc_rs_save_cb = NULL;
	M0_UT_ASSERT(addb_ut_ctx_gpt_cb_num == 3 + ARRAY_SIZE(addb_ut_def_ctx));
	M0_UT_ASSERT(addb_cdc_tlist_is_empty(&addb_cdc));

	/* fini */
	m0_addb_mc_fini(&mc1);
	m0_addb_mc_fini(&m0_addb_gmc);

	/* no unregister API: explicitly unlink the ut context types */
#undef CT_UNREG
#define CT_UNREG(n)							\
	addb_ct_tlist_del(&m0__addb_ut_ct##n);				\
	m0__addb_ut_ct##n.act_magic = 0;				\
	M0_UT_ASSERT(m0_addb_ctx_type_lookup(m0__addb_ut_ct##n.act_id) == NULL)

	CT_UNREG(9);
	CT_UNREG(8);
	CT_UNREG(7);
	CT_UNREG(6);
	CT_UNREG(5);
	CT_UNREG(4);
	CT_UNREG(3);
	CT_UNREG(2);
	CT_UNREG(1);
	CT_UNREG(0);

#undef CT_UNREG

	/* restore the global machine for other UTs */
	m0_addb_mc_init(&m0_addb_gmc);
	M0_UT_ASSERT(m0_addb_mc_is_initialized(&m0_addb_gmc));
}

/*
 ****************************************************************************
 * Test for global context init
 ****************************************************************************
 */
struct addb_ut_ctx_gci_thread_arg {
	struct m0_thread         t;
	int                      n;
	struct m0_addb_mc       *mc;
	struct m0_addb_ctx       ctx;
	struct m0_addb_ctx_type *ct;
	struct m0_addb_ctx      *pctx;
	size_t                   reclen;
	struct m0_semaphore     *sem;
};
static struct addb_ut_ctx_gci_thread_arg addb_ut_ctx_gci_ta[10];

/* initialization thread body */
static void addb_ut_ctx_gci_thread(struct addb_ut_ctx_gci_thread_arg *ta)
{
	m0_semaphore_down(ta->sem);
	switch (ta->n) {
	case 0:
		M0_ADDB_CTX_INIT(ta->mc, &ta->ctx, ta->ct, ta->pctx);
		break;
	case 1:
		M0_ADDB_CTX_INIT(ta->mc, &ta->ctx, ta->ct, ta->pctx, 1);
		break;
	case 2:
		M0_ADDB_CTX_INIT(ta->mc, &ta->ctx, ta->ct, ta->pctx, 1, 2);
		break;
	case 3:
		M0_ADDB_CTX_INIT(ta->mc, &ta->ctx, ta->ct, ta->pctx, 1, 2, 3);
		break;
	case 4:
		M0_ADDB_CTX_INIT(ta->mc, &ta->ctx, ta->ct, ta->pctx, 1, 2, 3,
				 4);
		break;
	case 5:
		M0_ADDB_CTX_INIT(ta->mc, &ta->ctx, ta->ct, ta->pctx, 1, 2, 3, 4,
				 5);
		break;
	case 6:
		M0_ADDB_CTX_INIT(ta->mc, &ta->ctx, ta->ct, ta->pctx, 1, 2, 3, 4,
				 5, 6);
		break;
	case 7:
		M0_ADDB_CTX_INIT(ta->mc, &ta->ctx, ta->ct, ta->pctx, 1, 2, 3, 4,
				 5, 6, 7);
		break;
	case 8:
		M0_ADDB_CTX_INIT(ta->mc, &ta->ctx, ta->ct, ta->pctx, 1, 2, 3, 4,
				 5, 6, 7, 8);
		break;
	case 9:
		M0_ADDB_CTX_INIT(ta->mc, &ta->ctx, ta->ct, ta->pctx, 1, 2, 3, 4,
				 5, 6, 7, 8, 9);
		break;
	default:
		M0_ASSERT(ta->n < 9);
	}
	return;
}

static int addb_ut_ctx_gci_cb_num;
static void addb_ut_ctx_gci_cb(const struct m0_addb_rec *rec)
{
	uint32_t ctxpathlen = 0;
	uint64_t ctxpath[4];
	int i;
	uint32_t fields_nr = 0;
	uint64_t *fields;
	struct addb_ut_ctx_gci_thread_arg *ta;

	if (addb_rec_post_ut_data.brt != M0_ADDB_BRT_CTXDEF)
		return;
	M0_UT_ASSERT(addb_rec_post_ut_data.cv_nr == 1);
	ta = container_of(addb_rec_post_ut_data.cv[0],
			  struct addb_ut_ctx_gci_thread_arg, ctx);

	M0_ASSERT(ta->n < ARRAY_SIZE(addb_ut_ctx_gci_ta));
	M0_ASSERT(&addb_ut_ctx_gci_ta[ta->n] == ta);

	ctxpathlen = 4;
	ctxpath[0] = m0_node_uuid.u_hi;
	ctxpath[1] = m0_node_uuid.u_lo;
	ctxpath[2] = m0_addb_proc_ctx.ac_id;
	ctxpath[3] = addb_rec_post_ut_data.cv[0]->ac_id;

	fields_nr = addb_rec_post_ut_data.fields_nr;
	fields    = addb_rec_post_ut_data.fields;

	M0_UT_ASSERT(rec->ar_rid == addb_rec_post_ut_data.rid);
	M0_UT_ASSERT(m0_addb_rec_rid_to_brt(rec->ar_rid) == M0_ADDB_BRT_CTXDEF);
	M0_UT_ASSERT(m0_addb_rec_rid_to_id(rec->ar_rid) ==
		     addb_rec_post_ut_data.cv[0]->ac_type->act_id);
	M0_UT_ASSERT(rec->ar_ctxids.acis_nr == 1);
	for (i = 0; i < ctxpathlen; ++i)
		M0_UT_ASSERT(rec->ar_ctxids.acis_data->au64s_data[i]
			     == ctxpath[i]);
	M0_UT_ASSERT(rec->ar_data.au64s_nr == fields_nr);
	for (i = 0; i < fields_nr; ++i)
		M0_UT_ASSERT(rec->ar_data.au64s_data[i] == fields[i]);

	M0_UT_ASSERT(addb_ut_mc_rs_rec_len == addb_rec_post_ut_data.reclen);
	ta->reclen = addb_ut_mc_rs_rec_len;

	++addb_ut_ctx_gci_cb_num;

	addb_rec_post_ut_data.brt = M0_ADDB_BRT_NR; /* reset */
}

static void addb_ut_ctx_init_test(void)
{
	struct m0_semaphore sem;
	struct m0_addb_mc mc = { 0 };
	struct m0_addb_ctx *pctx = &m0_addb_proc_ctx;
	struct m0_addb_ctx ctx;
	int i;
	int rc;
	uint64_t cntr;

	rc = m0_semaphore_init(&sem, 0);
	M0_ASSERT(rc == 0);

	addb_ut_mc_reset();
	m0_addb_mc_init(&mc);
	addb_ut_mc_configure_recsink(&mc);
	addb_ut_mc_configure_pt_evmgr(&mc);
	M0_UT_ASSERT(m0_addb_mc_is_fully_configured(&mc));

	/*
	 * Temporarily register the fake contexts from addb_ut_md.c.
	 * Remember it in the corresponding thread args.
	 */
#undef CT_REG
#define CT_REG(n)					\
	m0__addb_ut_ct##n.act_id = addb_ct_max_id + 1;	\
	addb_ut_ctx_gci_ta[n].ct = &m0__addb_ut_ct##n;	\
	m0_addb_ctx_type_register(&m0__addb_ut_ct##n)

	CT_REG(0);
	CT_REG(1);
	CT_REG(2);
	CT_REG(3);
	CT_REG(4);
	CT_REG(5);
	CT_REG(6);
	CT_REG(7);
	CT_REG(8);
	CT_REG(9);

#undef CT_REG

	/*
	 * Init the contexts, and validate the definition record.
	 * Use concurrent threads, because ADDB is supposed to serialize the
	 * parent counter internally for global context parents.
	 */
	cntr = pctx->ac_cntr;
	addb_ut_mc_rs_save_cb = addb_ut_ctx_gci_cb;
	for (i = 0; i < ARRAY_SIZE(addb_ut_ctx_gci_ta); ++i) {
		struct addb_ut_ctx_gci_thread_arg *ta = &addb_ut_ctx_gci_ta[i];
		ta->n = i;
		ta->mc = &mc;
		ta->sem = &sem;
		ta->pctx = pctx;
		M0_SET0(&ta->t);
		M0_ASSERT(ta->ct != NULL); /* set in CT_REG */
		M0_ASSERT(ta->reclen == 0);
		rc = M0_THREAD_INIT(&ta->t, struct addb_ut_ctx_gci_thread_arg *,
				    NULL, &addb_ut_ctx_gci_thread, ta,
				    "addb_ut_ctx%d", i);
		M0_ASSERT(rc == 0);
	}
	for (i = 0; i < ARRAY_SIZE(addb_ut_ctx_gci_ta); ++i)
		m0_semaphore_up(&sem); /* unblock threads */
	for (i = 0; i < ARRAY_SIZE(addb_ut_ctx_gci_ta); ++i) {
		struct addb_ut_ctx_gci_thread_arg *ta = &addb_ut_ctx_gci_ta[i];
		m0_thread_join(&ta->t);
		m0_thread_fini(&ta->t);
	}
	addb_ut_mc_rs_save_cb = NULL;
	M0_UT_ASSERT(addb_ut_ctx_gci_cb_num == 10);
	M0_UT_ASSERT(pctx->ac_cntr == cntr + 10);

	/* validate the content of the contexts */
#undef CTX_VALIDATE
#define CTX_VALIDATE(n)							\
	M0_UT_ASSERT(addb_ut_ctx_gci_ta[n].ctx.ac_magic == M0_ADDB_CTX_MAGIC); \
	M0_UT_ASSERT(addb_ut_ctx_gci_ta[n].ctx.ac_type == &m0__addb_ut_ct##n); \
	M0_UT_ASSERT(addb_ut_ctx_gci_ta[n].ctx.ac_parent == pctx);	\
	M0_UT_ASSERT(addb_ut_ctx_gci_ta[n].ctx.ac_imp_id == NULL);	\
	M0_UT_ASSERT(addb_ut_ctx_gci_ta[n].ctx.ac_cntr == 0);		\
	M0_UT_ASSERT(addb_ut_ctx_gci_ta[n].ctx.ac_depth == pctx->ac_depth + 1);\
	M0_UT_ASSERT(!m0_addb_ctx_is_imported(&addb_ut_ctx_gci_ta[n].ctx))

	CTX_VALIDATE(0);
	CTX_VALIDATE(1);
	CTX_VALIDATE(2);
	CTX_VALIDATE(3);
	CTX_VALIDATE(4);
	CTX_VALIDATE(5);
	CTX_VALIDATE(6);
	CTX_VALIDATE(7);
	CTX_VALIDATE(8);
	CTX_VALIDATE(9);

#undef CTX_VALIDATE

	/* record length is montonically increasing by field size */
	for (i = 0; i < ARRAY_SIZE(addb_ut_ctx_gci_ta); ++i) {
		struct addb_ut_ctx_gci_thread_arg *ta = &addb_ut_ctx_gci_ta[i];
		if (i == 0)
			M0_UT_ASSERT(ta->reclen > 0);
		else
			M0_UT_ASSERT(ta->reclen >=
				     addb_ut_ctx_gci_ta[i-1].reclen +
				     sizeof(uint64_t));
	}

	/* Use one of these contexts as the parent of another. */
	M0_ADDB_CTX_INIT(&mc, &ctx, &m0__addb_ut_ct0,
			 &addb_ut_ctx_gci_ta[0].ctx);
	M0_UT_ASSERT(ctx.ac_magic == M0_ADDB_CTX_MAGIC);
	M0_UT_ASSERT(ctx.ac_type == &m0__addb_ut_ct0);
	M0_UT_ASSERT(ctx.ac_parent == &addb_ut_ctx_gci_ta[0].ctx);
	M0_UT_ASSERT(ctx.ac_imp_id == NULL);
	M0_UT_ASSERT(ctx.ac_cntr == 0);
	M0_UT_ASSERT(ctx.ac_depth == addb_ut_ctx_gci_ta[0].ctx.ac_depth + 1);
	M0_UT_ASSERT(!m0_addb_ctx_is_imported(&ctx));
	M0_UT_ASSERT(addb_ut_ctx_gci_ta[0].ctx.ac_cntr == 1);

	/* fini the contexts */
	m0_addb_ctx_fini(&ctx);
	M0_UT_ASSERT(!addb_ctx_invariant(&ctx));
	for (i = 0; i < ARRAY_SIZE(addb_ut_ctx_gci_ta); ++i) {
		m0_addb_ctx_fini(&addb_ut_ctx_gci_ta[i].ctx);
		M0_UT_ASSERT(!addb_ctx_invariant(&addb_ut_ctx_gci_ta[i].ctx));
	}

	/* fini */
	m0_addb_mc_fini(&mc);

	/* no unregister API: explicitly unlink the ut context types */
#undef CT_UNREG
#define CT_UNREG(n)							\
	addb_ct_tlist_del(&m0__addb_ut_ct##n);				\
	M0_UT_ASSERT(m0_addb_ctx_type_lookup(m0__addb_ut_ct##n.act_id) == NULL)

	CT_UNREG(9);
	CT_UNREG(8);
	CT_UNREG(7);
	CT_UNREG(6);
	CT_UNREG(5);
	CT_UNREG(4);
	CT_UNREG(3);
	CT_UNREG(2);
	CT_UNREG(1);
	CT_UNREG(0);

#undef CT_UNREG
	m0_semaphore_fini(&sem);
}

/*
 ****************************************************************************
 * Test for context export / import
 ****************************************************************************
 */
static void addb_ut_ctx_import_export(void)
{
	struct m0_addb_ctx ctx;
	struct m0_addb_ctx *cp;
	struct m0_addb_uint64_seq id1 = { 0 };
	struct m0_addb_uint64_seq id2 = { 0 };
	int i;

	/* export from a normal context object */
	M0_UT_ASSERT(m0_addb_ctx_export(&m0_addb_proc_ctx, &id1) == 0);
	M0_UT_ASSERT(id1.au64s_nr == m0_addb_proc_ctx.ac_depth);
	for (i = m0_addb_proc_ctx.ac_depth - 1, cp = &m0_addb_proc_ctx; i <= 0;
	     ++i, cp = cp->ac_parent)
		M0_UT_ASSERT(id1.au64s_data[i] == cp->ac_id);

	/* import */
	M0_UT_ASSERT(m0_addb_ctx_import(&ctx, &id1) == 0);
	M0_UT_ASSERT(m0_addb_ctx_is_imported(&ctx));
	for (i = ctx.ac_depth - 1, cp = &m0_addb_proc_ctx; i <= 0;
	     ++i, cp = cp->ac_parent)
		M0_UT_ASSERT(ctx.ac_imp_id[i] == cp->ac_id);

	/* export from the imported context */
	M0_UT_ASSERT(m0_addb_ctx_export(&ctx, &id2) == 0);
	M0_UT_ASSERT(id2.au64s_nr == id1.au64s_nr);
	for (i = 0; i < id2.au64s_nr; ++i)
		M0_UT_ASSERT(id2.au64s_data[i] == id1.au64s_data[i]);

	/* fini */
	m0_addb_ctx_id_free(&id1);
	M0_UT_ASSERT(id1.au64s_nr == 0);
	M0_UT_ASSERT(id1.au64s_data == NULL);
	m0_addb_ctx_id_free(&id2);
	M0_UT_ASSERT(id2.au64s_nr == 0);
	M0_UT_ASSERT(id2.au64s_data == NULL);
	m0_addb_ctx_fini(&ctx);
	M0_UT_ASSERT(!addb_ctx_invariant(&ctx));
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
