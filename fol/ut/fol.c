/* -*- C -*- */

#include <stdlib.h>                /* system, free */

#include "lib/ut.h"
#include "lib/ub.h"
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

static int result;

static void test_init(void) 
{
	result = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(result == 0);

	result = c2_fol_init(&fol, &db);
	C2_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
	C2_ASSERT(result == 0);

	d = &r.fr_d;
	h = &d->rd_h;
}

static void test_fini(void)
{
	result = c2_db_tx_commit(&tx);
	C2_ASSERT(result == 0);

	c2_fol_fini(&fol);
	c2_dbenv_fini(&db);
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
}

static void test_lookup(void)
{
	struct c2_fol_rec dup;

	result = c2_fol_add(&fol, &tx, d);
	C2_ASSERT(result == 0);

	result = c2_fol_rec_lookup(&fol, &tx, d->rd_lsn, &dup);
	C2_ASSERT(result == 0);

	C2_ASSERT(dup.fr_d.rd_lsn == d->rd_lsn);
	C2_ASSERT(memcmp(&d->rd_h, &dup.fr_d.rd_h, sizeof d->rd_h) == 0);

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

#if 0
/*
 * UB
 */

enum {
	UB_ITER = 10000
};

static void ub_init(void)
{
	db_reset();
	test_init();
}

static void ub_fini(void)
{
	test_fini();
	db_reset();
}

static struct c2_uint128 p;

static void ub_obj_init(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	result = c2_emap_obj_insert(&emap, &tx, &p, 42);
	C2_ASSERT(result == 0);
	checkpoint();
}

static void ub_obj_fini(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	result = c2_emap_obj_delete(&emap, &tx, &p);
	C2_ASSERT(result == 0);
	checkpoint();
}

static void ub_obj_init_same(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	result = c2_emap_obj_insert(&emap, &tx, &p, 42);
	C2_ASSERT(result == 0);
}

static void ub_obj_fini_same(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	result = c2_emap_obj_delete(&emap, &tx, &p);
	C2_ASSERT(result == 0);
}

static void ub_split(int i)
{
	split(5000, 1, false);
}

struct c2_ub_set c2_emap_ub = {
	.us_name = "emap-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = { 
		{ .ut_name = "obj-init",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_obj_init },

		{ .ut_name = "obj-fini",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_obj_fini },

		{ .ut_name = "obj-init-same-tx",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_obj_init_same },

		{ .ut_name = "obj-fini-same-tx",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_obj_fini_same },

		{ .ut_name = "split",
		  .ut_iter = UB_ITER/5,
		  .ut_init = test_obj_init,
		  .ut_round = ub_split },

		{ .ut_name = NULL }
	}
};
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
