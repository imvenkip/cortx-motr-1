/* -*- C -*- */
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 16-Sep-2010
 */

#include "fol/fol.h"
#include "fol/fol_private.h"
#include "fol/fol_xc.h"
#if !XXX_USE_DB5
/* XXX FIXME: Do not use ut/ directory of other subsystem. */
#  include "be/ut/helper.h"   /* m0_be_ut_h */
#endif

#include "fid/fid_xc.h"
#include "rpc/rpc_opcodes.h"
#include "lib/memory.h"
#include "lib/misc.h"         /* M0_SET0 */

#include "ut/ut.h"
#include "lib/ub.h"

static struct m0_fol_rec_header *h;

#if XXX_USE_DB5
static const char db_name[] = "ut-fol";

static struct m0_fol           fol;
static struct m0_fol_rec       r;
static struct m0_fol_rec_desc *d;
static struct m0_buf           buf;
static struct m0_dbenv         db;
static struct m0_db_tx         tx;
static int                     rc;
#else
static struct m0_fol          *g_fol;
static struct m0_be_ut_backend g_ut_be;
static struct m0_be_ut_seg     g_ut_seg;
#endif

#if XXX_USE_DB5
static int db_reset(void)
{
	return m0_ut_db_reset(db_name);
}
#endif

static int verify_part_data(struct m0_fol_rec_part *part, struct m0_db_tx *tx);
M0_FOL_REC_PART_TYPE_DECLARE(ut_part, static, verify_part_data, NULL);

static void test_init(void)
{
#if XXX_USE_DB5
	rc = m0_dbenv_init(&db, db_name, 0);
	M0_ASSERT(rc == 0);

	rc = m0_fol_init(&fol, &db);
	M0_ASSERT(rc == 0);

	rc = m0_db_tx_init(&tx, &db, 0);
	M0_ASSERT(rc == 0);

	m0_fol_rec_init(&r);

	d = &r.fr_desc;
	h = &d->rd_header;
#else
	M0_BE_TX_CREDIT(cred);
	int rc;

	backend_init(&g_ut_be, &g_ut_seg);

	g_fol = be_alloc(sizeof *g_fol, &g_ut_seg.bus_seg);
	M0_UT_ASSERT(g_fol != NULL);

	m0_fol_credit(g_fol, M0_FO_INIT, &cred);
	tx_begin(&g_tx, &g_ut_be, &cred);

	rc = m0_fol_init(g_fol, &g_tx);
	M0_UT_ASSERT(rc == 0);
#endif
}

static void test_fini(void)
{
#if XXX_USE_DB5
	m0_fol_rec_fini(&r);

	rc = m0_db_tx_commit(&tx);
	M0_ASSERT(rc == 0);

	m0_fol_fini(&fol);
	m0_dbenv_fini(&db);
	m0_buf_free(&buf);
#else
	m0_fol_fini(g_fol);
	tx_end(&g_tx);

	be_free(g_fol);
	backend_fini(&g_ut_be, &g_ut_seg);
#endif
}

static void test_rec_part_type_reg(void)
{
	ut_part_type = M0_FOL_REC_PART_TYPE_XC_OPS("UT record part", m0_fid_xc,
						   &ut_part_type_ops);
	rc = m0_fol_rec_part_type_register(&ut_part_type);
	M0_ASSERT(rc == 0);
	M0_ASSERT(ut_part_type.rpt_index > 0);
}

static void test_rec_part_type_unreg(void)
{
	m0_fol_rec_part_type_deregister(&ut_part_type);
	M0_ASSERT(ut_part_type.rpt_ops == NULL);
	M0_ASSERT(ut_part_type.rpt_xt == NULL);
	M0_ASSERT(ut_part_type.rpt_index == 0);
}

#if XXX_USE_DB5
static void test_add(void)
{
	M0_SET0(h);
	h->rh_refcount = 1;

	d->rd_lsn = m0_fol_lsn_allocate(&fol);
	rc = m0_fol_rec_add(&fol, &tx, &r);
	M0_ASSERT(rc == 0);
}

extern m0_lsn_t lsn_inc(m0_lsn_t lsn);

static void test_lookup(void)
{
	struct m0_fol_rec dup;

	d->rd_lsn = m0_fol_lsn_allocate(&fol);
	rc = m0_fol_rec_add(&fol, &tx, &r);
	M0_ASSERT(rc == 0);

	rc = m0_fol_rec_lookup(&fol, &tx, d->rd_lsn, &dup);
	M0_ASSERT(rc == 0);

	M0_ASSERT(dup.fr_desc.rd_lsn == d->rd_lsn);
	M0_ASSERT(m0_xcode_cmp(&M0_REC_HEADER_XCODE_OBJ(&d->rd_header),
			       &M0_REC_HEADER_XCODE_OBJ(&dup.fr_desc.rd_header))
		  == 0);

	m0_fol_lookup_rec_fini(&dup);

	rc = m0_fol_rec_lookup(&fol, &tx, lsn_inc(d->rd_lsn), &dup);
	M0_ASSERT(rc == -ENOENT);
}
#endif

static int verify_part_data(struct m0_fol_rec_part *part, struct m0_db_tx *tx)
{
	struct m0_fid *dec_rec;

	dec_rec = part->rp_data;
	M0_UT_ASSERT(dec_rec->f_container == 22);
	M0_UT_ASSERT(dec_rec->f_key == 33);
	return 0;
}

#if XXX_USE_DB5
static void test_fol_rec_part_encdec(void)
{
	struct m0_fid          *rec;
	struct m0_fol_rec       dec_rec;
	struct m0_fol_rec_part  ut_rec_part;
	m0_lsn_t                lsn;
	struct m0_fol_rec_part *dec_part;

	m0_fol_rec_init(&r);

	M0_ALLOC_PTR(rec);
	M0_UT_ASSERT(rec != NULL);
	*rec = (struct m0_fid){ .f_container = 22, .f_key = 33 };

	m0_fol_rec_part_init(&ut_rec_part, rec, &ut_part_type);
	m0_fol_rec_part_add(&r, &ut_rec_part);

	h->rh_refcount = 1;
	lsn = d->rd_lsn = m0_fol_lsn_allocate(&fol);

	rc = m0_fol_rec_add(&fol, &tx, &r);
	M0_ASSERT(rc == 0);

	rc = m0_db_tx_commit(&tx);
	M0_ASSERT(rc == 0);
	m0_fol_rec_fini(&r);

	rc = m0_db_tx_init(&tx, &db, 0);
	M0_ASSERT(rc == 0);

	rc = m0_fol_rec_lookup(&fol, &tx, lsn, &dec_rec);
	M0_ASSERT(rc == 0);

	m0_tl_for(m0_rec_part, &dec_rec.fr_parts, dec_part) {
		/* Call verify_part_data() for each part. */
		dec_part->rp_ops->rpo_undo(dec_part, &tx);
	} m0_tl_endfor;

	m0_fol_lookup_rec_fini(&dec_rec);
}
#endif

#if !XXX_USE_DB5
static void noop(const struct m0_be_tx *tx) {}

static void *ut_be_alloc(m0_bcount_t size)
{
	enum { SHIFT = 0 };
	M0_BE_TX_CREDIT(cred);
	struct m0_be_tx tx;
	struct m0_be_op op;
	void           *ptr;

	/* Prepare the credit needed for the allocation. */
	m0_be_allocator_credit(beh.buh_allocator, M0_BAO_ALLOC, size, SHIFT,
			       &cred);

	/* Initialise the transaction. */
	m0_be_tx_init(&tx, ++beh.buh_tid, &beh.buh_be, &ut__txs_sm_group,
		      noop, noop, false /* "local" tx */, NULL, NULL);

	/* m0_be_tx_{prep,open,capture,close,timedwait}() require
	 * transaction's sm_group to be locked. */
	m0_sm_group_lock(&ut__txs_sm_group);

	/* Get transaction prepared by giving it the credit. */
	m0_be_tx_prep(&tx, &cred);

	/* Open the transaction and wait for it to become active. */
	m0_be_tx_open(&tx);
	rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_ACTIVE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);

	/* Allocate `size' bytes. */
	m0_be_op_init(&op);
	ptr = m0_be_alloc(beh.buh_allocator, &tx, &op, size, 0);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);

	/* Close the transaction and wait for its data to become written to
	 * segment. */
	m0_be_tx_close(&tx);
	rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_PLACED), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);

	/*
	 * XXX The placement of this statement does not look right.
	 * An attempt to get rid of it causes m0_be_ut_h_fini() to fail.
	 * BE UTs manage without finalising their transactions, somehow...
	 */
	m0_be_tx_fini(&tx);

	m0_sm_group_unlock(&ut__txs_sm_group);

	M0_POST(ptr != NULL);
	return ptr;
}

/* ------------------------------------------------------------------
 * BE paraphernalia
 * ------------------------------------------------------------------ */

static void backend_init(struct m0_be_ut_backend *be, struct m0_be_ut_seg *seg)
{
	m0_be_ut_backend_init(be);
	m0_be_ut_seg_init(seg, 1 << 20 /* 1 MB */);
	m0_be_ut_seg_allocator_init(seg, be);
}

static void backend_fini(struct m0_be_ut_backend *be, struct m0_be_ut_seg *seg)
{
	m0_be_ut_seg_allocator_fini(seg, be);
	m0_be_ut_seg_fini(seg);
	m0_be_ut_backend_fini(be);
}

static void tx_begin(struct m0_be_tx *tx, struct m0_be_ut_backend *ut_be,
		     struct m0_be_tx_credit *cred)
{
	int rc;

	m0_be_ut_tx_init(tx, ut_be);
	m0_be_tx_prep(tx, cred);
	m0_be_tx_open(tx);
	rc = m0_be_tx_timedwait(tx, M0_BTS_ACTIVE, M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
}

static void tx_end(struct m0_be_tx *tx)
{
	int rc;

	m0_be_tx_close(tx);
	rc = m0_be_tx_timedwait(tx, M0_BTS_DONE, M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	m0_be_tx_fini(tx);
}

/** Allocates `size' bytes on segment `seg' synchronously. */
static void *be_alloc(m0_bcount_t size, struct m0_be_seg *seg)
{
	enum { SHIFT = 0 };
	M0_BE_TX_CREDIT(cred);
	struct m0_be_tx tx;
	struct m0_be_op op;
	void           *ptr;
	int             rc;

	m0_be_allocator_credit(&seg->bs_allocator, M0_BAO_ALLOC, fol, SHIFT,
			       &cred);
	tx_begin(&tx, &g_ut_be, &cred);

	m0_be_op_init(&op);
	ptr = m0_be_alloc(&seg->bs_allocator, &tx, &op, size, SHIFT);
	rc = m0_be_op_wait(&op);
	M0_UT_ASSERT(rc == 0);
	m0_be_op_fini(&op);

	tx_end(&tx);
	return ptr;
}

/** Releases resources obtained by be_alloc(). */
static void be_free(void *ptr)
{
	enum { SHIFT = 0 };
	M0_BE_TX_CREDIT(cred);
	struct m0_be_tx tx;
	struct m0_be_op op;
	int             rc;

	m0_be_allocator_credit(&seg->bs_allocator, M0_BAO_FREE, fol, SHIFT,
			       &cred);
	tx_begin(&tx, &g_ut_be, &cred);

	m0_be_op_init(&op);
	m0_be_free(&seg->bs_allocator, &tx, &op, ptr);
	rc = m0_be_op_wait(&op);
	M0_UT_ASSERT(rc == 0);
	m0_be_op_fini(&op);

	tx_end(&tx);
}

/* ---------------------------------------------------------------------
 * XXX FIXME: Use m0_fom_simple instead of an "ast thread".
 * See the comment in be/ut/helper.c.
 * XXX Code duplication.
 */

static struct {
	bool             run;
	struct m0_thread thread;
} g_ast;

static void ast_thread(int _)
{
	struct m0_sm_group *g = &ut__txs_sm_group;

	while (g_ast.run) {
		m0_chan_wait(&g->s_clink);
		m0_sm_group_lock(g);
		m0_sm_asts_run(g);
		m0_sm_group_unlock(g);
	}
}

static int _init(void)
{
	m0_sm_group_init(&ut__txs_sm_group);
	g_ast.run = true;
	return M0_THREAD_INIT(&g_ast.thread, int, NULL, &ast_thread, 0,
			      "ast_thread");
}

static int _fini(void)
{
	g_ast.run = false;
	m0_clink_signal(&ut__txs_sm_group.s_clink);
	m0_thread_join(&g_ast.thread);
	m0_sm_group_fini(&ut__txs_sm_group);
	return 0;
}
#endif

const struct m0_test_suite fol_ut = {
	.ts_name = "fol-ut",
#if XXX_USE_DB5
	.ts_init = db_reset,
#else
	.ts_init = _init,
	.ts_fini = _fini,
#endif
	.ts_tests = {
		/*
		 * Note, that there are dependencies between these tests.
		 * Do not reorder them willy-nilly.
		 */
		{ "fol-init",                test_init                },
#if XXX_USE_DB5
		{ "fol-rec-part-type-reg",   test_rec_part_type_reg   },
		{ "fol-add",                 test_add                 },
		{ "fol-lookup",              test_lookup              },
		{ "fol-rec-part-test",       test_fol_rec_part_encdec },
		{ "fol-rec-part-type-unreg", test_rec_part_type_unreg },
#endif
		{ "fol-fini",                test_fini                },
		{ NULL, NULL }
	}
};

/* ------------------------------------------------------------------
 * UB
 * ------------------------------------------------------------------ */

enum { UB_ITER = 100000 };

static int ub_init(const char *opts M0_UNUSED)
{
#if XXX_USE_DB5
	db_reset();
#endif
	test_init();
	test_rec_part_type_reg();

	M0_SET0(h);

	h->rh_refcount = 1;
	return 0;
}

static void ub_fini(void)
{
	test_rec_part_type_unreg();
	test_fini();
#if XXX_USE_DB5
	db_reset();
#endif
}

#if XXX_USE_DB5
static m0_lsn_t last;

static void checkpoint()
{
	rc = m0_db_tx_commit(&tx);
	M0_ASSERT(rc == 0);

	rc = m0_db_tx_init(&tx, &db, 0);
	M0_ASSERT(rc == 0);
}

static void ub_insert(int i)
{
	d->rd_lsn = m0_fol_lsn_allocate(&fol);
	rc = m0_fol_rec_add(&fol, &tx, &r);
	M0_ASSERT(rc == 0);
	last = d->rd_lsn;
	if (i % 1000 == 0)
		checkpoint();
}

static void ub_lookup(int i)
{
	m0_lsn_t lsn;
	struct m0_fol_rec rec;

	lsn = last - i;

	rc = m0_fol_rec_lookup(&fol, &tx, lsn, &rec);
	M0_ASSERT(rc == 0);
	m0_fol_lookup_rec_fini(&rec);
	if (i % 1000 == 0)
		checkpoint();
}

static void ub_insert_buf(int i)
{
	d->rd_lsn = m0_fol_lsn_allocate(&fol);
	rc = m0_fol_add_buf(&fol, &tx, d, &buf);
	M0_ASSERT(rc == 0);
	if (i % 1000 == 0)
		checkpoint();
}
#endif

struct m0_ub_set m0_fol_ub = {
	.us_name = "fol-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
#if XXX_USE_DB5
		{ .ub_name = "insert",
		  .ub_iter = UB_ITER,
		  .ub_round = ub_insert },

		{ .ub_name = "lookup",
		  .ub_iter = UB_ITER,
		  .ub_round = ub_lookup },

		{ .ub_name = "insert-buf",
		  .ub_iter = UB_ITER,
		  .ub_round = ub_insert_buf },
#endif
		{ .ub_name = NULL }
	}
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
