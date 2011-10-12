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
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 10/11/2011
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "ioservice/cobfid_map.h"
#include "lib/ut.h"

struct c2_dbenv cfm_dbenv;
struct c2_addb_ctx cfm_addb_ctx;
struct c2_cobfid_map cfm_map;
struct c2_cobfid_map_iter cfm_iter;
struct c2_dbenv cfm_dbenv;

uint64_t container_id;
struct c2_fid file_fid;
struct c2_uint128 cob_fid;

static const char cfm_map_path[] = "cfm_map";
static int rc;

/* C2_UT_ASSERT not usable during ts_init */
static int cfm_ut_init(void)
{
	rc = c2_ut_db_reset(cfm_map_path);
	C2_ASSERT(rc == 0);

        rc = c2_dbenv_init(&cfm_dbenv, cfm_map_path, 0);
	C2_ASSERT(rc == 0);

	rc = c2_cobfid_map_init(&cfm_map, &cfm_dbenv, &cfm_addb_ctx, "cfm_map");
	C2_ASSERT(rc == 0);

	return rc;
}

static int cfm_ut_fini(void)
{
	c2_dbenv_fini(&cfm_dbenv);
	return 0;
}

static void cfm_ut_insert(void)
{
	container_id = 100;
	file_fid.f_container = 0;
	file_fid.f_key = 0;
	cob_fid.u_hi = 111;
	cob_fid.u_lo = 0;
	rc = c2_cobfid_map_add(&cfm_map, container_id, file_fid, cob_fid);
	C2_UT_ASSERT(rc == 0);
}

static void cfm_ut_delete(void)
{
	container_id = 100;
	file_fid.f_container = 0;
	file_fid.f_key = 0;
	rc = c2_cobfid_map_del(&cfm_map, container_id, file_fid);
	C2_UT_ASSERT(rc == 0);
}

static void cfm_ut_enumerate(void)
{
}

const struct c2_test_suite cfm_ut = {
	.ts_name = "libcfm-ut",
	.ts_init = cfm_ut_init,
	.ts_fini = cfm_ut_fini,
	.ts_tests = {
		{ "insert to cobfid_map", cfm_ut_insert },
		{ "delete from cobfid_map", cfm_ut_delete },
		{ "enumerate cobfid_map", cfm_ut_enumerate },
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
