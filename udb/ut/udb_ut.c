#include "lib/cdefs.h"
#include "lib/types.h"
#include "lib/ut.h"
#include "lib/misc.h" /* C2_SET0() */

#include "udb/udb.h"

static void cred_init(struct c2_udb_cred *cred,
		      enum c2_udb_cred_type type,
		      struct c2_udb_domain *dom)
{
	cred->uc_type = type;
	cred->uc_domain = dom;
}

static void cred_fini(struct c2_udb_cred *cred)
{
}

static bool cred_cmp(struct c2_udb_cred *left,
		     struct c2_udb_cred *right)
{
	return
		left->uc_type == right->uc_type &&
		left->uc_domain == right->uc_domain;
}

static void udb_test(void)
{
	int ret;
	struct c2_udb_domain dom;
	struct c2_udb_ctxt   ctx;
	struct c2_udb_cred   external;
	struct c2_udb_cred   internal;
	struct c2_udb_cred   testcred;

	cred_init(&external, C2_UDB_CRED_EXTERNAL, &dom);
	cred_init(&internal, C2_UDB_CRED_INTERNAL, &dom);

	ret = c2_udb_ctxt_init(&ctx);
	C2_UT_ASSERT(ret == 0);

	/* add mapping */
	ret = c2_udb_add(&ctx, &dom, &external, &internal);
	C2_UT_ASSERT(ret == 0);

	C2_SET0(&testcred);
	ret = c2_udb_e2i(&ctx, &external, &testcred);
	/* means that mapping exists */
	C2_UT_ASSERT(ret == 0);
	/* successfully mapped */
	C2_UT_ASSERT(cred_cmp(&internal, &testcred));

	C2_SET0(&testcred);
	ret = c2_udb_i2e(&ctx, &internal, &testcred);
	/* means that mapping exists */
	C2_UT_ASSERT(ret == 0);
	/* successfully mapped */
	C2_UT_ASSERT(cred_cmp(&external, &testcred));

	/* delete mapping */
	ret = c2_udb_del(&ctx, &dom, &external, &internal);
	C2_UT_ASSERT(ret == 0);

	/* check that mapping does not exist */
	C2_SET0(&testcred);
	ret = c2_udb_e2i(&ctx, &external, &testcred);
	C2_UT_ASSERT(ret != 0);

	/* check that mapping does not exist */
	C2_SET0(&testcred);
	ret = c2_udb_i2e(&ctx, &internal, &testcred);
	C2_UT_ASSERT(ret != 0);

	cred_fini(&internal);
	cred_fini(&external);
	c2_udb_ctxt_fini(&ctx);
}

const struct c2_test_suite udb_ut = {
        .ts_name = "udb-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "udb", udb_test },
                { NULL, NULL }
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
