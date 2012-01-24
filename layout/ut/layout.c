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
 * Original author: Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 12/21/2011
 */

#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/memory.h"
#include "lib/misc.h"              /* C2_SET0 */
#include "lib/bitstring.h"
#include "layout/layout.h"
#include "layout/layout_db.h"

static const char            db_name[] = "ut-layout";
static struct c2_ldb_schema  schema;
static struct c2_dbenv       dbenv;
static int                   rc;

extern const struct c2_layout_type c2_pdclust_layout_type;
extern const struct c2_layout_type c2_composite_layout_type;

static int test_init(void)
{
	c2_ut_db_reset(db_name);

	rc = c2_dbenv_init(&dbenv, db_name, 0);
	C2_UT_ASSERT(rc == 0);

	rc = c2_ldb_schema_init(&schema, &dbenv);
	C2_UT_ASSERT(rc == 0);


	/* test_init is called by ub_init which hates C2_UT_ASSERT */
	/* C2_ASSERT(rc == 0); */

	c2_ldb_type_register(&schema, &c2_pdclust_layout_type);
	c2_ldb_type_register(&schema, &c2_composite_layout_type);

	return rc;
}

static int test_fini(void)
{
	c2_ldb_type_unregister(&schema, &c2_pdclust_layout_type);
	c2_ldb_type_unregister(&schema, &c2_composite_layout_type);

	rc = c2_ldb_schema_fini(&schema);
	C2_UT_ASSERT(rc == 0);

	c2_dbenv_fini(&dbenv);

	return 0;
}

const struct c2_layout_type test_layout_type = {
	.lt_name     = "test",
	.lt_id       = 0x544553544C41594F, /* TESTLAYO */
	.lt_ops      = NULL
};

static void test_type_reg_unreg(void)
{
	struct c2_layout_type *lt;

	/* Register a layout type. */
	c2_ldb_type_register(&schema, &test_layout_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(IS_IN_ARRAY(test_layout_type.lt_id, schema.ls_type));

	lt = schema.ls_type[test_layout_type.lt_id];
	C2_UT_ASSERT(lt == &test_layout_type);

	/* Should be able to unregister it. */
	c2_ldb_type_unregister(&schema, &test_layout_type);
	C2_UT_ASSERT(!IS_IN_ARRAY(test_layout_type.lt_id, schema.ls_type));

	/* Should be able to register it again. */
	c2_ldb_type_register(&schema, &test_layout_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(IS_IN_ARRAY(test_layout_type.lt_id, schema.ls_type));

	lt = schema.ls_type[test_layout_type.lt_id];
	C2_UT_ASSERT(lt == &test_layout_type);

	/* Should not be able to register it without unregistering it. */
	c2_ldb_type_register(&schema, &test_layout_type);
	C2_UT_ASSERT(rc != 0);

	/* Unregister it. */
	c2_ldb_type_unregister(&schema, &test_layout_type);
	C2_UT_ASSERT(!IS_IN_ARRAY(test_layout_type.lt_id, schema.ls_type));
}

const struct c2_layout_enum_type test_enum_type = {
	.let_name    = "test",
	.let_id      = 0x54455354454E554D , /* TESTENUM */
	.let_ops     = NULL
};

static void test_etype_reg_unreg(void)
{
	struct c2_layout_enum_type *le;

	/* Register a layout enum type. */
	c2_ldb_enum_register(&schema, &test_enum_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(IS_IN_ARRAY(test_enum_type.let_id, schema.ls_enum));

	le = schema.ls_enum[test_enum_type.let_id];
	C2_UT_ASSERT(le == &test_enum_type);

	/* Should be able to unregister it. */
	c2_ldb_enum_unregister(&schema, &test_enum_type);
	C2_UT_ASSERT(!IS_IN_ARRAY(test_enum_type.let_id, schema.ls_enum));

	/* Should be able to register it again. */
	c2_ldb_enum_register(&schema, &test_enum_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(IS_IN_ARRAY(test_enum_type.let_id, schema.ls_enum));

	le = schema.ls_enum[test_enum_type.let_id];
	C2_UT_ASSERT(le == &test_enum_type);

	/* Should not be able to register it without unregistering it. */
	c2_ldb_enum_register(&schema, &test_enum_type);
	C2_UT_ASSERT(rc != 0);

	/* Unregister it. */
	c2_ldb_enum_unregister(&schema, &test_enum_type);
	C2_UT_ASSERT(!IS_IN_ARRAY(test_enum_type.let_id, schema.ls_enum));
}

static void test_schema_init_fini(void)
{
	const char            t_db_name[] = "t-layout";
	struct c2_ldb_schema  t_schema;
	struct c2_dbenv       t_dbenv;

	rc = c2_dbenv_init(&t_dbenv, t_db_name, 0);
	C2_UT_ASSERT(rc == 0);

	/* Initialize the schema. */
	rc = c2_ldb_schema_init(&t_schema, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	/* Should be able finalize it. */
	rc = c2_ldb_schema_fini(&schema);
	C2_UT_ASSERT(rc == 0);

	/* Should be able to initialize it again. */
	rc = c2_ldb_schema_init(&t_schema, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	/* Register a layout type. */
	c2_ldb_type_register(&t_schema, &test_layout_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(IS_IN_ARRAY(test_layout_type.lt_id, t_schema.ls_type));

	/* Register a layout enum type. */
	c2_ldb_enum_register(&t_schema, &test_enum_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(IS_IN_ARRAY(test_enum_type.let_id, t_schema.ls_enum));

	/*
	 * Should not be able finalize it since a layout type and an enum
	 * type are still registered.
	 */
	rc = c2_ldb_schema_fini(&t_schema);
	C2_UT_ASSERT(rc != 0);

	/* Unregister the layout type. */
	c2_ldb_type_unregister(&t_schema, &test_layout_type);
	C2_UT_ASSERT(!IS_IN_ARRAY(test_layout_type.lt_id, t_schema.ls_type));

	/*
	 * Should still not be able finalize it since an enum type is still
	 * registered.
	 */
	rc = c2_ldb_schema_fini(&t_schema);
	C2_UT_ASSERT(rc != 0);

	/* Unregister the enum type. */
	c2_ldb_enum_unregister(&t_schema, &test_enum_type);
	C2_UT_ASSERT(!IS_IN_ARRAY(test_enum_type.let_id, t_schema.ls_enum));

	/* Should now be able to finalize it. */
	rc = c2_ldb_schema_fini(&t_schema);
	C2_UT_ASSERT(rc == 0);

	c2_dbenv_fini(&t_dbenv);
}



static void test_encode(void)
{
}

static void test_decode(void)
{
}

static void test_add(void)
{
}

static void test_lookup(void)
{
}

static void test_update(void)
{
}

static void test_delete(void)
{
}

static void test_persistence(void)
{
}

const struct c2_test_suite layout_ut = {
	.ts_name  = "layout-ut",
	.ts_init  = test_init,
	.ts_fini  = test_fini,
	.ts_tests = {
		{ "layout-type-register-unregister", test_type_reg_unreg },
		{ "layout-etype-register-unregister", test_etype_reg_unreg },
		{ "layout-schema-init-fini", test_schema_init_fini },
		{ "layout-encode", test_encode },
		{ "layout-decode", test_decode },
                { "layout-add", test_add },
                { "layout-lookup", test_lookup },
                { "layout-update", test_update },
                { "layout-delete", test_delete },
                { "layout-persistence", test_persistence },
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
