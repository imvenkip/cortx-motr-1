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

static const char db_name[] = "ut-layout";

static struct c2_dbenv       db;
static int rc;

static int db_reset(void)
{
       return rc;
}

static void test_type_register(void)
{

}

static void test_type_unregister(void)
{

}

static void test_etype_register(void)
{

}

static void test_etype_unregister(void)
{

}

static void test_init(void)
{
	rc = c2_dbenv_init(&db, db_name, 0);
	/* test_init is called by ub_init which hates C2_UT_ASSERT */
	C2_ASSERT(rc == 0);

}

static void test_fini(void)
{
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
	.ts_name = "layout-ut",
	.ts_init = db_reset,
	/* .ts_fini = db_reset, */
	.ts_tests = {
		{ "layout-type-register", test_type_register },
		{ "layout-type-unregister", test_type_unregister },
		{ "layout-etype-register", test_etype_register },
		{ "layout-etype-unregister", test_etype_unregister },
		{ "layout-init", test_init },
		{ "layout-fini", test_fini },
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
