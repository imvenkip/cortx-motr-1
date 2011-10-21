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


/* Number of records to be enumerated */
enum {
	SINGLE_BUF_REC_NR = 16,
	MULTIPLE_BUF_REC_NR = 351
};

/* Variables used for simple table insert-delete checks */
uint64_t container_id;
struct c2_fid file_fid;
struct c2_uint128 cob_fid;

static const char single_buf_cont_enum_path[] = "cfm_map_single_buf_ce";
static const char multiple_buf_cont_enum_path[] = "cfm_map_multiple_buf_ce";
static const char single_buf_map_enum_path[] = "cfm_map_single_buf_me";
static const char multiple_buf_map_enum_path[] = "cfm_map_multiple_buf_me";
static int rc;

#if 0
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
#endif

static void container_enumerate(int rec_total, const char *map_path)
{
	int			 rec_nr;
	int			 i;
	int			 j;
	uint64_t		 container_id_out;
	struct c2_fid		*fid_in;
	struct c2_uint128	*cob_fid_in;
	struct c2_fid		*fid_out;
	struct c2_uint128	*cob_fid_out;
	struct c2_dbenv		 cfm_dbenv;
	struct c2_addb_ctx	 cfm_addb_ctx;
	struct c2_cobfid_map	 cfm_map;
	struct c2_cobfid_map_iter cfm_iter;

	rc = c2_ut_db_reset(map_path);
	C2_UT_ASSERT(rc == 0);

        rc = c2_dbenv_init(&cfm_dbenv, map_path, 0);
	C2_UT_ASSERT(rc == 0);

	rc = c2_cobfid_map_init(&cfm_map, &cfm_dbenv, &cfm_addb_ctx,
				"cfm_map_table");
	C2_UT_ASSERT(rc == 0);

	C2_ALLOC_ARR(fid_in, rec_total);
	C2_UT_ASSERT(fid_in != NULL);

	C2_ALLOC_ARR(cob_fid_in, rec_total);
	C2_UT_ASSERT(cob_fid_in != NULL);

	C2_ALLOC_ARR(fid_out, rec_total);
	C2_UT_ASSERT(fid_out != NULL);

	C2_ALLOC_ARR(cob_fid_out, rec_total);
	C2_UT_ASSERT(cob_fid_out != NULL);

	container_id = 200;
	j = rec_total - 1;

	/* Fill in the database for same container id and varying fid values in
	   decreasing order of fid key. This is done on purpose to test the
	   ordering property where enumeration should be done in increasing
	   order of the fid key.*/
	for (i = 0; i < rec_total; i++) {
		/* Populate some random data */
		fid_in[i].f_container = 0;
		fid_in[i].f_key = j;
		cob_fid_in[i].u_hi = 333;
		cob_fid_in[i].u_lo = j;

		j--;

		rc = c2_cobfid_map_add(&cfm_map, container_id, fid_in[i],
				       cob_fid_in[i]);
		C2_UT_ASSERT(rc == 0);
		printf("\nADD: rc = %d, ci = %lu fid = %lu cfid = %lu",
				rc, container_id, fid_in[i].f_key,
				cob_fid_in[i].u_lo);
	}

	rc = c2_cobfid_map_container_enum(&cfm_map, container_id, &cfm_iter);
	C2_UT_ASSERT(rc == 0);

	rec_nr = 0;
	while ((rc = c2_cobfid_map_iter_next(&cfm_iter, &container_id_out,
					&fid_out[rec_nr],
					&cob_fid_out[rec_nr])) == 0) {
		printf("\nENUM: rc = %d, ci = %lu fid = %lu cfid_out = %lu",
				rc, container_id_out, fid_out[rec_nr].f_key,
				cob_fid_out[rec_nr].u_lo);
		rec_nr++;
	}
	/* Check if number of records enumerated is same as number of records
	   inserted */
	printf("\nrec_nr = %d\n",rec_nr);
	C2_UT_ASSERT(rec_nr == rec_total);

	/* Check if the fid and cob input arrays are exact reverse of their
	   corresponding out counterparts */
	for (i = 0; i < rec_total; i++) {
		C2_UT_ASSERT(c2_fid_eq(&fid_in[i],
				       &fid_out[rec_total - 1 - i]));
		C2_UT_ASSERT(c2_uint128_eq(&cob_fid_in[i],
					   &cob_fid_out[rec_total - 1 - i]));
	}

	c2_free(fid_in);
	c2_free(cob_fid_in);
	c2_free(fid_out);
	c2_free(cob_fid_out);

	c2_cobfid_map_iter_fini(&cfm_iter);
	c2_cobfid_map_fini(&cfm_map);
}

void cfm_ut_container_enumerate(void)
{
	container_enumerate(SINGLE_BUF_REC_NR, single_buf_cont_enum_path);
	container_enumerate(MULTIPLE_BUF_REC_NR, multiple_buf_cont_enum_path);
}

const struct c2_test_suite cfm_ut = {
	.ts_name = "cfm-ut",
	.ts_init = NULL, 
	.ts_fini = NULL,
	.ts_tests = {
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
