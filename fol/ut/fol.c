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
#  include "ut/be.h"
#  include "ut/ast_thread.h"
/* XXX FIXME: Do not use ut/ directory of other subsystem. */
#  include "be/ut/helper.h"   /* m0_be_ut_backend */
#endif

#include "fid/fid_xc.h"
#include "rpc/rpc_opcodes.h"
#include "lib/memory.h"
#include "lib/misc.h"         /* M0_SET0 */

#include "ut/ut.h"
#include "lib/ub.h"

#if XXX_USE_DB5
static const char db_name[] = "ut-fol";

static struct m0_fol_rec_header *h;
static struct m0_fol_rec_desc   *d;
static struct m0_fol             fol;
static struct m0_fol_rec         r;
<<<<<<< HEAD
=======
static struct m0_fol_rec_desc   *d;
static struct m0_fol_rec_header *hh;
>>>>>>> Eliminate static variables named "c" and "h".
static struct m0_buf             buf;
static struct m0_dbenv           db;
static struct m0_db_tx           tx;
#else
static struct m0_fol            *g_fol;
static struct m0_fol_rec_header *g_hdr;
static struct m0_fol_rec_desc   *g_desc;
static struct m0_fol_rec         g_rec;
static struct m0_be_ut_backend   g_ut_be;
static struct m0_be_ut_seg       g_ut_seg;
static struct m0_be_tx           g_tx;
#endif

#if XXX_USE_DB5
static int db_reset(void)
{
	return m0_ut_db_reset(db_name);
}
#endif

static int verify_part_data(struct m0_fol_rec_part *part,
#if XXX_USE_DB5
			    struct m0_db_tx *tx
#else
			    struct m0_be_tx *tx
#endif
	);
M0_FOL_REC_PART_TYPE_DECLARE(ut_part, static, verify_part_data, NULL,
				NULL, NULL);

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
	struct m0_be_tx_credit cred = {};
	int		       rc;

	m0_ut_backend_init(&g_ut_be, &g_ut_seg);

	g_fol = m0_ut_be_alloc(sizeof *g_fol, &g_ut_seg.bus_seg, &g_ut_be);
	M0_UT_ASSERT(g_fol != NULL);

	m0_fol_init(g_fol, &g_ut_seg.bus_seg);
	m0_fol_credit(g_fol, M0_FO_CREATE, 1, &cred);
	/*
	 * There are 3 m0_fol_rec_add() calls --- in functions test_add(),
	 * test_lookup(), and test_fol_rec_part_encdec().
	 */
	m0_fol_credit(g_fol, M0_FO_REC_ADD, 3, &cred);
	m0_ut_be_tx_begin(&g_tx, &g_ut_be, &cred);

	M0_BE_OP_SYNC(op,
		      rc = m0_fol_create(g_fol, &g_tx, &op));
	M0_UT_ASSERT(rc == 0);

	m0_fol_rec_init(&g_rec);
	g_desc = &g_rec.fr_desc;
	g_hdr = &g_desc->rd_header;
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
	struct m0_be_tx_credit cred = {};

	m0_fol_rec_fini(&g_rec);
	m0_ut_be_tx_end(&g_tx);

	m0_fol_credit(g_fol, M0_FO_DESTROY, 1, &cred);
	m0_ut_be_tx_begin(&g_tx, &g_ut_be, &cred);
	M0_BE_OP_SYNC(op, m0_fol_destroy(g_fol, &g_tx, &op));
	m0_ut_be_tx_end(&g_tx);
	m0_fol_fini(g_fol);

	m0_ut_be_free(g_fol, sizeof *g_fol, &g_ut_seg.bus_seg, &g_ut_be);
	m0_ut_backend_fini(&g_ut_be, &g_ut_seg);
#endif
}

static void test_rec_part_type_reg(void)
{
	int rc;

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

static void test_add(void)
{
#if XXX_USE_DB5
	M0_SET0(h);

	hh->rh_refcount = 1;

	d->rd_lsn = m0_fol_lsn_allocate(&fol);
	rc = m0_fol_rec_add(&fol, &tx, &r);
	M0_ASSERT(rc == 0);
#else
	int rc;

	M0_SET0(g_hdr);
	g_hdr->rh_refcount = 1;

	g_desc->rd_lsn = m0_fol_lsn_allocate(g_fol);
	M0_BE_OP_SYNC(op, rc = m0_fol_rec_add(g_fol, &g_rec, &g_tx, &op));
	M0_ASSERT(rc == 0);
#endif
}

extern m0_lsn_t lsn_inc(m0_lsn_t lsn);

static void test_lookup(void)
{
#if XXX_USE_DB5
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
#else
	struct m0_fol_rec dup;
	int               rc;

	g_desc->rd_lsn = m0_fol_lsn_allocate(g_fol);
	M0_BE_OP_SYNC(op, rc = m0_fol_rec_add(g_fol, &g_rec, &g_tx, &op));
	M0_ASSERT(rc == 0);

	rc = m0_fol_rec_lookup(g_fol, g_desc->rd_lsn, &dup);
	M0_ASSERT(rc == 0);

	M0_ASSERT(dup.fr_desc.rd_lsn == g_desc->rd_lsn);
	M0_ASSERT(m0_xcode_cmp(&M0_REC_HEADER_XCODE_OBJ(&g_desc->rd_header),
			       &M0_REC_HEADER_XCODE_OBJ(&dup.fr_desc.rd_header))
		  == 0);

	m0_fol_lookup_rec_fini(&dup);

	rc = m0_fol_rec_lookup(g_fol, lsn_inc(g_desc->rd_lsn), &dup);
	M0_ASSERT(rc == -ENOENT);
#endif
}

static int verify_part_data(struct m0_fol_rec_part *part,
#if XXX_USE_DB5
			    struct m0_db_tx *_
#else
			    struct m0_be_tx *_
#endif
	)
{
	struct m0_fid *dec_rec;

	dec_rec = part->rp_data;
	M0_UT_ASSERT(dec_rec->f_container == 22);
	M0_UT_ASSERT(dec_rec->f_key == 33);
	return 0;
}

static void test_fol_rec_part_encdec(void)
{
	struct m0_fid          *rec;
	struct m0_fol_rec       dec_rec;
	struct m0_fol_rec_part  ut_rec_part;
	m0_lsn_t                lsn;
	struct m0_fol_rec_part *dec_part;
#if !XXX_USE_DB5
	int                     rc;

	m0_fol_rec_init(&g_rec);
#else
	m0_fol_rec_init(&r);
#endif

	M0_ALLOC_PTR(rec);
	M0_UT_ASSERT(rec != NULL);
	*rec = (struct m0_fid){ .f_container = 22, .f_key = 33 };

	m0_fol_rec_part_init(&ut_rec_part, rec, &ut_part_type);
#if XXX_USE_DB5
	m0_fol_rec_part_add(&r, &ut_rec_part);

	hh->rh_refcount = 1;
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
#else
	m0_fol_rec_part_add(&g_rec, &ut_rec_part);

	g_hdr->rh_refcount = 1;
	lsn = g_desc->rd_lsn = m0_fol_lsn_allocate(g_fol);

	M0_BE_OP_SYNC(op, rc = m0_fol_rec_add(g_fol, &g_rec, &g_tx, &op));
	M0_ASSERT(rc == 0);

	m0_fol_rec_fini(&g_rec);

	m0_ut_be_tx_end(&g_tx);
	m0_ut_be_tx_begin(&g_tx, &g_ut_be, &M0_BE_TX_CREDIT(0, 0));

	rc = m0_fol_rec_lookup(g_fol, lsn, &dec_rec);
	M0_ASSERT(rc == 0);

	m0_tl_for(m0_rec_part, &dec_rec.fr_parts, dec_part) {
		/* Call verify_part_data() for each part. */
		dec_part->rp_ops->rpo_undo(dec_part, &g_tx);
	} m0_tl_endfor;
#endif
	m0_fol_lookup_rec_fini(&dec_rec);
}

#if !XXX_USE_DB5
extern struct m0_sm_group ut__txs_sm_group;

static int _init(void)
{
	m0_sm_group_init(&ut__txs_sm_group);
	return m0_ut_ast_thread_start(&ut__txs_sm_group);
}

static int _fini(void)
{
	m0_ut_ast_thread_stop();
	m0_sm_group_fini(&ut__txs_sm_group);
	return 0;
}
#endif /* XXX_USE_DB5 */

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
		{ "fol-rec-part-type-reg",   test_rec_part_type_reg   },
		{ "fol-add",                 test_add                 },
		{ "fol-lookup",              test_lookup              },
		{ "fol-rec-part-test",       test_fol_rec_part_encdec },
		{ "fol-rec-part-type-unreg", test_rec_part_type_unreg },
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
	test_init();
	test_rec_part_type_reg();

	M0_SET0(h);

<<<<<<< HEAD
	h->rh_refcount = 1;
#else
	test_init();
	test_rec_part_type_reg();
	*g_hdr = (struct m0_fol_rec_header){ .rh_refcount = 1 };
#endif
=======
	hh->rh_refcount = 1;
>>>>>>> Eliminate static variables named "c" and "h".
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
