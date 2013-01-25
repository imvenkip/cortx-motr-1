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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 09/16/2010
 */


#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/memory.h"
#include "lib/misc.h"              /* M0_SET0 */
#include "fol/fol.h"
#include "rpc/rpc_opcodes.h"
#include "fid/fid_xc.h"
#include "fol/fol_xc.h"

static const char db_name[] = "ut-fol";

static int db_reset(void)
{
        return m0_ut_db_reset(db_name);
}

static struct m0_dbenv       db;
static struct m0_fol         fol;
static struct m0_dtx         dtx;

static struct m0_fol_rec           *r;
static struct m0_fol_rec_desc      *d;
static struct m0_fol_rec_header    *h;
static struct m0_buf                buf;

static int result;

static void test_init(void)
{
	result = m0_dbenv_init(&db, db_name, 0);
	M0_ASSERT(result == 0);

	result = m0_fol_init(&fol, &db);
	M0_ASSERT(result == 0);

	m0_dtx_init(&dtx);
	result = m0_dtx_open(&dtx, &db);
	M0_ASSERT(result == 0);

	r = dtx.tx_fol_rec;
	d = &r->fr_desc;
	h = &d->rd_header;
}

static void test_fini(void)
{
	result = m0_dtx_done(&dtx);
	M0_ASSERT(result == 0);

	m0_fol_fini(&fol);
	m0_dbenv_fini(&db);
	m0_free(buf.b_addr);
}

static void test_add(void)
{
	M0_SET0(h);

	h->rh_refcount = 1;

	d->rd_lsn = m0_fol_lsn_allocate(&fol);
	result = m0_fol_rec_add(&fol, &dtx);
	M0_ASSERT(result == 0);
}

extern m0_lsn_t lsn_inc(m0_lsn_t lsn);
#define REC_HEADER_XCODE_OBJ(ptr) M0_XCODE_OBJ(m0_fol_rec_header_xc, ptr)

static void test_lookup(void)
{
	struct m0_fol_rec dup;

	d->rd_lsn = m0_fol_lsn_allocate(&fol);
	result = m0_fol_rec_add(&fol, &dtx);
	M0_ASSERT(result == 0);

	result = m0_fol_rec_lookup(&fol, &dtx.tx_dbtx, d->rd_lsn, &dup);
	M0_ASSERT(result == 0);

	M0_ASSERT(dup.fr_desc.rd_lsn == d->rd_lsn);
	M0_ASSERT(m0_xcode_cmp(&REC_HEADER_XCODE_OBJ(&d->rd_header),
			       &REC_HEADER_XCODE_OBJ(&dup.fr_desc.rd_header))
	          == 0);

	m0_rec_fini(&dup);

	result = m0_fol_rec_lookup(&fol, &dtx.tx_dbtx, lsn_inc(d->rd_lsn), &dup);
	M0_ASSERT(result == -ENOENT);
}

static void test_type_unreg(void)
{
	m0_fol_rec_type_unregister(&ut_fol_type);
}

int verify_part_data(struct m0_fol_rec_part *part)
{
	struct m0_fid *dec_rec;

	dec_rec = part->rp_data;
	M0_UT_ASSERT(dec_rec->f_container == 22);
	M0_UT_ASSERT(dec_rec->f_key == 33);
	return 0;
}

M0_FOL_REC_PART_TYPE_DECLARE(ut_part, verify_part_data, NULL);

static void test_fol_rec_part_encdec(void)
{
	struct m0_fid	       *rec;
	struct m0_fol_rec       dec_rec;
	struct m0_fol_rec_part *ut_rec_part;
	m0_lsn_t		lsn;
	struct m0_fol_rec_part *dec_part;

	M0_ALLOC_PTR(ut_rec_part);

	ut_part_type = M0_FOL_REC_PART_TYPE_XC_OPS("UT record part", m0_fid_xc,
						   &ut_part_type_ops);

	result =  m0_fol_rec_part_type_register(&ut_part_type);
	M0_ASSERT(result == 0);

	ut_rec_part = m0_fol_rec_part_init(&ut_part_type);
	M0_ASSERT(ut_rec_part != NULL);

	rec = ut_rec_part->rp_data;
	rec->f_container = 22;
	rec->f_key	 = 33;

	m0_fol_rec_part_add(dtx.tx_fol_rec, ut_rec_part);

	d->rd_type = &ut_fol_type;
	h->rh_refcount = 1;
	lsn = d->rd_lsn = m0_fol_lsn_allocate(&fol);

	result = m0_fol_rec_add(&fol, &dtx);
	M0_ASSERT(result == 0);

	result = m0_dtx_done(&dtx);
	M0_ASSERT(result == 0);

	m0_dtx_init(&dtx);
	result = m0_dtx_open(&dtx, &db);
	M0_ASSERT(result == 0);

	result = m0_fol_rec_lookup(&fol, &dtx.tx_dbtx, lsn, &dec_rec);
	M0_ASSERT(result == 0);

	m0_tl_for(m0_rec_part, &dec_rec.fr_fol_rec_parts, dec_part)
	{
		dec_part->rp_ops->rpo_undo(dec_part);
	} m0_tl_endfor;

	m0_fol_rec_part_type_fini(&ut_part_type);
	m0_rec_fini(&dec_rec);
}

const struct m0_test_suite fol_ut = {
	.ts_name = "fol-ut",
	.ts_init = db_reset,
	/* .ts_fini = db_reset, */
	.ts_tests = {
		{ "fol-init", test_init },
		{ "fol-rec-part-type-reg", test_rec_part_type_reg },
		{ "fol-add", test_add },
		{ "fol-lookup", test_lookup },
		{ "fol-part-rec-test", test_fol_rec_part_encdec},
		{ "fol-type-unreg", test_type_unreg },
		{ "fol-fini", test_fini },
		{ NULL, NULL }
	}
};

/*
 * UB
 */

enum {
	UB_ITER = 100000
};

static void ub_init(void)
{
	db_reset();
	test_init();

	M0_SET0(h);

	h->rh_refcount = 1;
}

static void ub_fini(void)
{
	test_fini();
	db_reset();
}

static m0_lsn_t last;

static void checkpoint()
{
	result = m0_dtx_done(&dtx);
	M0_ASSERT(result == 0);

	m0_dtx_init(&dtx);
	result = m0_dtx_open(&dtx, &db);
	M0_ASSERT(result == 0);
}

static void ub_insert(int i)
{
	d->rd_lsn = m0_fol_lsn_allocate(&fol);
	result = m0_fol_rec_add(&fol, &dtx);
	M0_ASSERT(result == 0);
	last = d->rd_lsn;
	if (i%1000 == 0)
		checkpoint();
}

static void ub_lookup(int i)
{
	m0_lsn_t lsn;
	struct m0_fol_rec rec;

	lsn = last - i;

	result = m0_fol_rec_lookup(&fol, &dtx.tx_dbtx, lsn, &rec);
	M0_ASSERT(result == 0);
	m0_rec_fini(&rec);
	if (i%1000 == 0)
		checkpoint();
}

static void ub_insert_buf(int i)
{
	d->rd_lsn = m0_fol_lsn_allocate(&fol);
	result = m0_fol_add_buf(&fol, &dtx.tx_dbtx, d, &buf);
	M0_ASSERT(result == 0);
	if (i%1000 == 0)
		checkpoint();
}

struct m0_ub_set m0_fol_ub = {
	.us_name = "fol-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ub_name = "insert",
		  .ub_iter = UB_ITER,
		  .ub_round = ub_insert },

		{ .ub_name = "lookup",
		  .ub_iter = UB_ITER,
		  .ub_round = ub_lookup },

		{ .ub_name = "insert-buf",
		  .ub_iter = UB_ITER,
		  .ub_round = ub_insert_buf },

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
