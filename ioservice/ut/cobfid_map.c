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

#include <sys/stat.h>
#include <stdlib.h>
#include "ioservice/cobfid_map.h"
#include "lib/ut.h"
#include "lib/memory.h"

struct c2_dbenv cfm_dbenv;
struct c2_addb_ctx cfm_addb_ctx;
struct c2_cobfid_map cfm_map;
struct c2_cobfid_map_iter cfm_iter;
struct c2_dbenv cfm_dbenv;

/* Number of records to be enumerated */
enum {
	REC_NR = 21
};

/* Variables used for simple table insert-delete checks */
uint64_t container_id;
struct c2_fid file_fid;
struct c2_uint128 cob_fid;

/* Variables used for container-enumeration */
struct c2_fid fid[REC_NR];
struct c2_uint128 cfid[REC_NR];
uint64_t ci_out;
struct c2_fid fid_out[REC_NR];
struct c2_uint128 cfid_out[REC_NR];

static const char cfm_map_path[] = "cfm_map";
static int rc;

/* C2_UT_ASSERT not usable during ts_init */
static int cfm_ut_init(void)
{
	rc = c2_ut_db_reset(cfm_map_path);
	C2_ASSERT(rc == 0);

        rc = c2_dbenv_init(&cfm_dbenv, cfm_map_path, 0);
	C2_ASSERT(rc == 0);

	rc = c2_cobfid_map_init(&cfm_map, &cfm_dbenv, &cfm_addb_ctx,
				"cfm_map_table");
	C2_ASSERT(rc == 0);

	return rc;
}

static int cfm_ut_fini(void)
{

	c2_cobfid_map_fini(&cfm_map);

	return 0;
}

static void cfm_ut_insert(void)
{
	container_id = 100;
	file_fid.f_container = 83;
	file_fid.f_key = 38;
	cob_fid.u_hi = 111;
	cob_fid.u_lo = 222;
	rc = c2_cobfid_map_add(&cfm_map, container_id, file_fid, cob_fid);
	C2_UT_ASSERT(rc == 0);
}

static void cfm_ut_delete(void)
{
	container_id = 100;
	file_fid.f_container = 83;
	file_fid.f_key = 38;
	rc = c2_cobfid_map_del(&cfm_map, container_id, file_fid);
	C2_UT_ASSERT(rc == 0);
}

static void cfm_ut_container_enumerate(void)
{
	int	 rec_nr;
	int	 i;
	int	 j;
	uint64_t ci;

	ci = 200;
	j = REC_NR - 1;
	/* Fill in the database for same container id and
	   varying fid values */
	for (i = 0; i < REC_NR; i++) {
		fid[i].f_container = 0;
		fid[i].f_key = j;
		cfid[i].u_hi = 333;
		cfid[i].u_lo = j;
		j--;
		rc = c2_cobfid_map_add(&cfm_map, ci, fid[i], cfid[i]);
		printf("\nADD: rc = %d, ci = %lu fid = %lu cfid = %lu",
				rc, ci, fid[i].f_key,
				cfid[i].u_lo);
		C2_UT_ASSERT(rc == 0);
	}
	rc = c2_dbenv_sync(&cfm_dbenv);
	C2_UT_ASSERT(rc == 0);

	rc = c2_cobfid_map_container_enum(&cfm_map, ci, &cfm_iter);
	C2_UT_ASSERT(rc == 0);

	rec_nr = 0;
	while ((rc = c2_cobfid_map_iter_next(&cfm_iter, &ci_out,
					&fid_out[rec_nr],
					&cfid_out[rec_nr])) == 0) {
		printf("\nENUM: rc = %d, ci = %lu fid = %lu cfid_out = %lu",
				rc, ci_out, fid_out[rec_nr].f_key,
				cfid_out[rec_nr].u_lo);
		rec_nr++;
	}
	/* Check if number of records enumerated is same as number of records
	   inserted */
	printf("\nrec_nr = %d\n",rec_nr);
	C2_UT_ASSERT(rec_nr == REC_NR);
}

const struct c2_test_suite cfm_ut = {
	.ts_name = "cfm-ut",
	.ts_init = cfm_ut_init,
	.ts_fini = cfm_ut_fini,
	.ts_tests = {
		{ "cfm-insert", cfm_ut_insert },
		{ "cfm-delete", cfm_ut_delete },
		{ "cfm-container-enumerate", cfm_ut_container_enumerate },
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
