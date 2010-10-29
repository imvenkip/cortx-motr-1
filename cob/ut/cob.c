/* -*- C -*- */

#include <stdlib.h>                /* system, free */

#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/memory.h"
#include "lib/misc.h"              /* C2_SET0 */
#include "cob/cob.h"

static const char db_name[] = "ut-cob";

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
static struct c2_cob_domain  dom;
static struct c2_cob         *cob;
static int rc;

static void test_init(void)
{
        struct c2_cob_domain_id id = { 42 };

	rc = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(rc == 0);

        rc = c2_cob_domain_init(&dom, &db, &id);
	C2_ASSERT(rc == 0);
}

static void test_fini(void)
{
        c2_cob_domain_fini(&dom);
	c2_dbenv_fini(&db);
}

static void make_nskey(struct c2_cob_nskey *key, uint64_t hi, uint64_t lo,
                       char *name)
{
        key->cnk_pfid.si_bits.u_hi = hi;
        key->cnk_pfid.si_bits.u_lo = lo;
        c2_bitstring_buf_set(&key->cnk_name, name);
        c2_bitstring_len_set(&key->cnk_name, strlen(name));
}

static void test_create(void)
{
        struct c2_cob_nskey key;
        struct c2_cob_nsrec nsrec;

        /* pfid, filename */
        make_nskey(&key, 0x123, 0x456, "hello world");

        nsrec.cnr_stobid.si_bits.u_hi = 0xabc;
        nsrec.cnr_stobid.si_bits.u_lo = 0xdef;
        nsrec.cnr_nlink = 1;

	rc = c2_cob_create(&dom, &key, &nsrec, &cob);
	C2_ASSERT(rc == 0);
        c2_cob_put(cob);
}

/* Lookup by name, make sure cfid is right */
static void test_lookup(void)
{
        struct c2_cob_nskey key;

        make_nskey(&key, 0x123, 0x456, "hello world");
        rc = c2_cob_lookup(&dom, &key, &cob);
        C2_ASSERT(rc == 0);
        C2_ASSERT(cob != NULL);
        C2_ASSERT(cob->co_dom == &dom);
        C2_ASSERT(cob->co_valid & CA_NSREC);
        C2_ASSERT(cob->co_nsrec != NULL);
        C2_ASSERT(cob->co_nsrec->cnr_stobid.si_bits.u_hi == 0xabc);
        C2_ASSERT(cob->co_nsrec->cnr_stobid.si_bits.u_lo == 0xdef);

        /* We should have cached the key also, unless oom */
        C2_ASSERT(cob->co_valid & CA_NSKEY);

        c2_cob_put(cob);
}

static int test_locate_internal(void)
{
        struct c2_stob_id    sid;

        /* stob fid */
        sid.si_bits.u_hi = 0xabc;
        sid.si_bits.u_lo = 0xdef;

        return c2_cob_locate(&dom, &sid, &cob);
}

/* Lookup by fid, make sure pfid is right */
static void test_locate(void)
{
        rc = test_locate_internal();
        C2_ASSERT(rc == 0);
        C2_ASSERT(cob != NULL);
        C2_ASSERT(cob->co_dom == &dom);

        /* We should have saved the NSKEY */
        C2_ASSERT(cob->co_valid & CA_NSKEY);
        C2_ASSERT(cob->co_nskey != NULL);
        C2_ASSERT(cob->co_nskey->cnk_pfid.si_bits.u_hi == 0x123);
        C2_ASSERT(cob->co_nskey->cnk_pfid.si_bits.u_lo == 0x456);

        /* Assuming we looked up the NSREC at the same time */
        C2_ASSERT(cob->co_valid & CA_NSREC);

        c2_cob_put(cob);
}

static void test_delete(void)
{
        /* gets ref */
        rc = test_locate_internal();
        C2_ASSERT(rc == 0);

        /* drops ref */
	rc = c2_cob_delete(cob);
	C2_ASSERT(rc == 0);

        /* should fail now */
        rc = test_locate_internal();
        C2_ASSERT(rc != 0);
}

const struct c2_test_suite cob_ut = {
	.ts_name = "cob-ut",
	.ts_init = db_reset,
	/* .ts_fini = db_reset, */
	.ts_tests = {
		{ "cob-init", test_init },
                { "cob-create", test_create },
                { "cob-lookup", test_lookup },
                { "cob-locate", test_locate },
                { "cob-delete", test_delete },
		{ "cob-fini", test_fini },
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
}

static void ub_fini(void)
{
	test_fini();
	db_reset();
}

static void ub_create(int i)
{
        struct c2_cob_nskey key;
        struct c2_cob_nsrec nsrec;

        /* pfid == cfid for data objects, so here we are identifying
           uniquely in the namespace by {pfid, ""} */
        make_nskey(&key, 0xAA, i, "");

        nsrec.cnr_stobid.si_bits.u_hi = 0xAA;
        nsrec.cnr_stobid.si_bits.u_lo = i;
        nsrec.cnr_nlink = 1;

	rc = c2_cob_create(&dom, &key, &nsrec, &cob);
	C2_ASSERT(rc == 0);

        c2_cob_put(cob);
}

static void ub_lookup(int i)
{
        struct c2_cob_nskey key;

        /* pfid == cfid for data objects */
        make_nskey(&key, 0xAA, i, "");
        rc = c2_cob_lookup(&dom, &key, &cob);
        C2_ASSERT(rc == 0);
        C2_ASSERT(cob != NULL);
        C2_ASSERT(cob->co_dom == &dom);

        C2_ASSERT(cob->co_valid & CA_NSREC);
        C2_ASSERT(cob->co_nsrec != NULL);
        C2_ASSERT(cob->co_nsrec->cnr_stobid.si_bits.u_hi == 0xAA);
        C2_ASSERT(cob->co_nsrec->cnr_stobid.si_bits.u_lo == i);

        /* We should have cached the key also, unless oom */
        C2_ASSERT(cob->co_valid & CA_NSKEY);

        c2_cob_put(cob);
}


struct c2_ub_set c2_cob_ub = {
	.us_name = "cob-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ut_name = "create",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_create },

		{ .ut_name = "lookup",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_lookup },

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
