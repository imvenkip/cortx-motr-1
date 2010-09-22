/* -*- C -*- */

#include <stdlib.h>                /* system, free */

#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/memory.h"
#include "lib/misc.h"              /* C2_SET0 */
#include "fol/fol.h"

static const char db_name[] = "ut-fol";

static int db_reset(void)
{
	char *cmd;
	int   rc;

	rc = asprintf(&cmd, "rm -fr \"%s\"", db_name);
	C2_ASSERT(rc > 0);

	rc = system(cmd);
	C2_ASSERT(rc == 0);
	free(cmd);
	return 0;
}

static struct c2_dbenv       db;
static struct c2_fol         fol;
static struct c2_db_tx       tx;

static struct c2_fol_rec            r;
static struct c2_fol_rec_desc      *d;
static struct c2_fol_rec_header    *h;
static struct c2_buf                buf;

static int result;

static void test_init(void) 
{
	result = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(result == 0);

	result = c2_fol_init(&fol, &db);
	C2_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
	C2_ASSERT(result == 0);

	d = &r.fr_desc;
	h = &d->rd_header;
}

static void test_fini(void)
{
	result = c2_db_tx_commit(&tx);
	C2_ASSERT(result == 0);

	c2_fol_fini(&fol);
	c2_dbenv_fini(&db);
	c2_free(buf.b_addr);
}

static size_t ut_fol_size(struct c2_fol_rec_desc *desc)
{
	return 160;
}

static void ut_fol_pack(struct c2_fol_rec_desc *desc, void *buf)
{
	memset(buf, 'y', 160);
}

static const struct c2_fol_rec_type_ops ut_fol_ops = {
	.rto_pack_size = ut_fol_size,
	.rto_pack      = ut_fol_pack
};

static const struct c2_fol_rec_type ut_fol_type = {
	.rt_name   = "ut-fol-rec",
	.rt_opcode = 5,
	.rt_ops    = &ut_fol_ops
};

static void test_type_reg(void)
{
	result = c2_fol_rec_type_register(&ut_fol_type);
	C2_ASSERT(result == 0);
}

static void test_add(void)
{
	C2_SET0(h);

	d->rd_type = &ut_fol_type;
	h->rh_refcount = 1;

	result = c2_fol_add(&fol, &tx, d);
	C2_ASSERT(result == 0);

	result = c2_fol_rec_pack(d, &buf);
	C2_ASSERT(result == 0);
}

static void test_lookup(void)
{
	struct c2_fol_rec dup;

	result = c2_fol_add(&fol, &tx, d);
	C2_ASSERT(result == 0);

	result = c2_fol_rec_lookup(&fol, &tx, d->rd_lsn, &dup);
	C2_ASSERT(result == 0);

	C2_ASSERT(dup.fr_desc.rd_lsn == d->rd_lsn);
	C2_ASSERT(memcmp(&d->rd_header, 
			 &dup.fr_desc.rd_header, sizeof d->rd_header) == 0);

	c2_fol_rec_fini(&dup);

	result = c2_fol_rec_lookup(&fol, &tx, c2_lsn_inc(d->rd_lsn), &dup);
	C2_ASSERT(result == -ENOENT);
}

static void test_type_unreg(void)
{
	c2_fol_rec_type_unregister(&ut_fol_type);
}


const struct c2_test_suite fol_ut = {
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

	C2_SET0(h);

	d->rd_type = &ut_fol_type;
	h->rh_refcount = 1;
}

static void ub_fini(void)
{
	test_type_unreg();
	test_fini();
	db_reset();
}

static c2_lsn_t last;

static void checkpoint()
{
	result = c2_db_tx_commit(&tx);
	C2_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
	C2_ASSERT(result == 0);
}

static void ub_insert(int i)
{
	result = c2_fol_add(&fol, &tx, d);
	C2_ASSERT(result == 0);
	last = d->rd_lsn;
	if (i%1000 == 0)
		checkpoint();
}

static void ub_lookup(int i)
{
	c2_lsn_t lsn;
	struct c2_fol_rec rec;

	lsn = last - i;

	result = c2_fol_rec_lookup(&fol, &tx, lsn, &rec);
	C2_ASSERT(result == 0);
	c2_fol_rec_fini(&rec);
	if (i%1000 == 0)
		checkpoint();
}

static void ub_insert_buf(int i)
{
	result = c2_fol_add_buf(&fol, &tx, d, &buf);
	C2_ASSERT(result == 0);
	if (i%1000 == 0)
		checkpoint();
}

struct c2_ub_set c2_fol_ub = {
	.us_name = "fol-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = { 
		{ .ut_name = "insert",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_insert },

		{ .ut_name = "lookup",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_lookup },

		{ .ut_name = "insert-buf",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_insert_buf },

		{ .ut_name = NULL }
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
