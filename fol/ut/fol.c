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

static const char db_name[] = "ut-fol";

static int db_reset(void)
{
        return m0_ut_db_reset(db_name);
}

static struct m0_dbenv       db;
static struct m0_fol         fol;
static struct m0_db_tx       tx;

static struct m0_fol_rec            r;
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

	result = m0_db_tx_init(&tx, &db, 0);
	M0_ASSERT(result == 0);

	d = &r.fr_desc;
	h = &d->rd_header;
}

static void test_fini(void)
{
	result = m0_db_tx_commit(&tx);
	M0_ASSERT(result == 0);

	m0_fol_fini(&fol);
	m0_dbenv_fini(&db);
	m0_free(buf.b_addr);
}

static const struct m0_fol_rec_type_ops ut_fol_ops = {
};

static const struct m0_fol_rec_type ut_fol_type = {
	.rt_name   = "ut-fol-rec",
	.rt_opcode = M0_FOL_UT_OPCODE,
	.rt_ops    = &ut_fol_ops
};

static void test_type_reg(void)
{
	result = m0_fol_rec_type_register(&ut_fol_type);
	M0_ASSERT(result == 0);
}

static void test_add(void)
{
	M0_SET0(h);

	d->rd_type = &ut_fol_type;
	h->rh_refcount = 1;

	d->rd_lsn = m0_fol_lsn_allocate(&fol);
	result = m0_fol_rec_add(&fol, &tx, &r);
	M0_ASSERT(result == 0);
}

extern m0_lsn_t lsn_inc(m0_lsn_t lsn);

static void test_lookup(void)
{
	struct m0_fol_rec dup;

	d->rd_lsn = m0_fol_lsn_allocate(&fol);
	result = m0_fol_rec_add(&fol, &tx, &r);
	M0_ASSERT(result == 0);

	result = m0_fol_rec_lookup(&fol, &tx, d->rd_lsn, &dup);
	M0_ASSERT(result == 0);

	M0_ASSERT(dup.fr_desc.rd_lsn == d->rd_lsn);
	M0_ASSERT(memcmp(&d->rd_header,
			 &dup.fr_desc.rd_header, sizeof d->rd_header) == 0);

	m0_rec_fini(&dup);

	result = m0_fol_rec_lookup(&fol, &tx, lsn_inc(d->rd_lsn), &dup);
	M0_ASSERT(result == -ENOENT);
}

static void test_type_unreg(void)
{
	m0_fol_rec_type_unregister(&ut_fol_type);
}


const struct m0_test_suite fol_ut = {
	.ts_name = "fol-ut",
	.ts_init = db_reset,
	/* .ts_fini = db_reset, */
	.ts_tests = {
		{ "fol-init", test_init },
		{ "fol-type-reg", test_type_reg },
		{ "fol-add", test_add },
		{ "fol-lookup", test_lookup },
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
	test_type_reg();

	M0_SET0(h);

	d->rd_type = &ut_fol_type;
	h->rh_refcount = 1;
}

static void ub_fini(void)
{
	test_type_unreg();
	test_fini();
	db_reset();
}

static m0_lsn_t last;

static void checkpoint()
{
	result = m0_db_tx_commit(&tx);
	M0_ASSERT(result == 0);

	result = m0_db_tx_init(&tx, &db, 0);
	M0_ASSERT(result == 0);
}

static void ub_insert(int i)
{
	d->rd_lsn = m0_fol_lsn_allocate(&fol);
	result = m0_fol_rec_add(&fol, &tx, &r);
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

	result = m0_fol_rec_lookup(&fol, &tx, lsn, &rec);
	M0_ASSERT(result == 0);
	m0_rec_fini(&rec);
	if (i%1000 == 0)
		checkpoint();
}

static void ub_insert_buf(int i)
{
	d->rd_lsn = m0_fol_lsn_allocate(&fol);
	result = m0_fol_add_buf(&fol, &tx, d, &buf);
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
