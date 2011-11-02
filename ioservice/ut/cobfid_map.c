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
#include "lib/memory.h"
#include "lib/arith.h"

/* Number of records to be enumerated */
enum {
	SINGLE_BUF_REC_NR = 16,
	MULTIPLE_BUF_REC_NR = 351
};

/* Type of enumeration */
enum {
	ENUM_CONTAINER,
	ENUM_MAP
};

/* DB paths for various databases */
static const char single_buf_cont_enum_path[] = "./cfm_map_single_buf_ce";
static const char multiple_buf_cont_enum_path[] = "./cfm_map_multiple_buf_ce";
static const char single_buf_map_enum_path[] = "./cfm_map_single_buf_me";
static const char multiple_buf_map_enum_path[] = "./cfm_map_multiple_buf_me";
static const char iter_test_map[] = "./cfm_map_iter_test";

/*
   Generic enumeration routine, used by all the tests.
   The parameters are :
   rec_total: Total number of records to be inserted and enumerated
   map_path : Database path used for the addition and enumeration
   etype    : Enumeration type (either map or container)
   check_persistence : Flag to check the database persistence
 */
static void enumerate_generic(int rec_total, const char *map_path, int etype,
			      bool check_persistence)
{
	int			 i;
	int			 j;
	int			 rc;
	int			 rec_nr;
	uint64_t		 container_id_in;
	uint64_t		 container_id_out;
	uint64_t		*cid_in;
	uint64_t		*cid_out;
	struct c2_fid		*fid_in;
	struct c2_fid		*fid_out;
	struct c2_uint128	*cob_fid_in;
	struct c2_uint128	*cob_fid_out;
	struct c2_dbenv		 cfm_dbenv;
	struct c2_db_tx		 cfm_dbtx;
	struct c2_addb_ctx	 cfm_addb_ctx;
	struct c2_cobfid_map	 cfm_map;
	struct c2_cobfid_map_iter cfm_iter;

	/* Reset any existing database */
	rc = c2_ut_db_reset(map_path);
	C2_UT_ASSERT(rc == 0);

	/* Initialise the database with given path */
        rc = c2_dbenv_init(&cfm_dbenv, map_path, 0);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_init(&cfm_dbtx, &cfm_dbenv, 0);
	C2_UT_ASSERT(rc == 0);

	/* Initialize the map */
	rc = c2_cobfid_map_init(&cfm_map, &cfm_dbenv, &cfm_addb_ctx,
				"cfm_map_table");
	C2_UT_ASSERT(rc == 0);

	/* Allocate the input and output key-value arrays with total number
	   of records.
	   - Input arrays are populated during insertion operation.
	   - Corresponding output key-value arrays are populated
	     during enumeration operation.
	   - Input and Output arrays are compared at the end for equality */
	C2_ALLOC_ARR(fid_in, rec_total);
	C2_UT_ASSERT(fid_in != NULL);

	C2_ALLOC_ARR(cob_fid_in, rec_total);
	C2_UT_ASSERT(cob_fid_in != NULL);

	C2_ALLOC_ARR(fid_out, rec_total);
	C2_UT_ASSERT(fid_out != NULL);

	C2_ALLOC_ARR(cob_fid_out, rec_total);
	C2_UT_ASSERT(cob_fid_out != NULL);

	/* Allocate the container arrays only for map enumeration. Use constant
	   container id for container enumeration*/
	if (etype == ENUM_MAP) {
		C2_ALLOC_ARR(cid_in, rec_total);
		C2_UT_ASSERT(cid_in != NULL);

		C2_ALLOC_ARR(cid_out, rec_total);
		C2_UT_ASSERT(cid_out != NULL);
	} else
		container_id_in = 200;
	j = rec_total - 1;

	/* Fill in the database with varying fid values in
	   decreasing order of fid key. This is done on purpose to test the
	   ordering property where enumeration should be done in increasing
	   order of the fid key.*/
	for (i = 0; i < rec_total; i++) {
		/* Populate some random data */
		fid_in[i].f_container = 0;
		fid_in[i].f_key = j;
		cob_fid_in[i].u_hi = 333;
		cob_fid_in[i].u_lo = j;


		if (etype == ENUM_MAP) {
			cid_in[i] = j;
			rc = c2_cobfid_map_add(&cfm_map, cid_in[i],
					       fid_in[i], cob_fid_in[i],
					       &cfm_dbtx);
		} else {
			rc = c2_cobfid_map_add(&cfm_map, container_id_in,
					       fid_in[i], cob_fid_in[i],
					       &cfm_dbtx);
		}
		C2_UT_ASSERT(rc == 0);
		j--;
	}

	/* Finalise the DB environment, transaction and cobfid_map,
	   and reinitialize to check database persistence */
	if (check_persistence) {
		c2_cobfid_map_fini(&cfm_map);
		c2_db_tx_commit(&cfm_dbtx);
		c2_dbenv_fini(&cfm_dbenv);

		/* Initialise the database with given path */
		rc = c2_dbenv_init(&cfm_dbenv, map_path, 0);
		C2_UT_ASSERT(rc == 0);

		/* Initialize the map */
		rc = c2_cobfid_map_init(&cfm_map, &cfm_dbenv, &cfm_addb_ctx,
				"cfm_map_table");
		C2_UT_ASSERT(rc == 0);

		rc = c2_db_tx_init(&cfm_dbtx, &cfm_dbenv, 0);
		C2_UT_ASSERT(rc == 0);
	}

	rec_nr = 0;
	/* Container enumeration */
	if (etype == ENUM_CONTAINER) {
		rc = c2_cobfid_map_container_enum(&cfm_map, container_id_in,
						  &cfm_iter, &cfm_dbtx);
		C2_UT_ASSERT(rc == 0);
		while ((rc = c2_cobfid_map_iter_next(&cfm_iter,
						&container_id_out,
						&fid_out[rec_nr],
						&cob_fid_out[rec_nr])) == 0) {
			rec_nr++;
		}
		C2_UT_ASSERT(cfm_iter.cfmi_error == -ENOENT);
		C2_UT_ASSERT(rc == -ENOENT);
	} else if (etype == ENUM_MAP) { /* Map enumeration */
		rc = c2_cobfid_map_enum(&cfm_map, &cfm_iter, &cfm_dbtx);
		C2_UT_ASSERT(rc == 0);
		while ((rc = c2_cobfid_map_iter_next(&cfm_iter,
						&cid_out[rec_nr],
						&fid_out[rec_nr],
						&cob_fid_out[rec_nr])) == 0) {
			rec_nr++;
		}
		C2_UT_ASSERT(cfm_iter.cfmi_error == -ENOENT);
		C2_UT_ASSERT(rc == -ENOENT);
	}

	c2_cobfid_map_iter_fini(&cfm_iter);

	/* Check if number of records enumerated is same as number of records
	   inserted */
	C2_UT_ASSERT(rec_nr == rec_total);

	/* Check if the fid and cob input arrays are exact reverse of their
	   corresponding out counterparts. Also check the cid array in case
	   of map enumeration*/
	for (i = 0; i < rec_total; i++) {
		C2_UT_ASSERT(c2_fid_eq(&fid_in[i],
				       &fid_out[rec_total - 1 - i]));
		C2_UT_ASSERT(c2_uint128_eq(&cob_fid_in[i],
					   &cob_fid_out[rec_total - 1 - i]));
		if (etype == ENUM_MAP) {
			C2_UT_ASSERT(C2_3WAY(cid_in[i],
					     cid_out[rec_total - 1 - i] == 0));
		}
	}

	c2_free(fid_in);
	c2_free(cob_fid_in);
	c2_free(fid_out);
	c2_free(cob_fid_out);

	if (etype == ENUM_MAP) {
		c2_free(cid_in);
		c2_free(cid_out);
	}

	c2_cobfid_map_fini(&cfm_map);
	c2_db_tx_commit(&cfm_dbtx);
	c2_dbenv_fini(&cfm_dbenv);

	rc = c2_ut_db_reset(map_path);
	C2_UT_ASSERT(rc == 0);
}

/* Container enumeration - single buffer fetch by iterator */
void ce_single_buf(void)
{
	enumerate_generic(SINGLE_BUF_REC_NR, single_buf_cont_enum_path,
			  ENUM_CONTAINER, false);
	enumerate_generic(SINGLE_BUF_REC_NR, single_buf_cont_enum_path,
			  ENUM_CONTAINER, true);
}

/* Container enumeration - multiple buffer fetches by iterator */
void ce_multiple_buf(void)
{
	enumerate_generic(MULTIPLE_BUF_REC_NR, multiple_buf_cont_enum_path,
			  ENUM_CONTAINER, false);
	enumerate_generic(MULTIPLE_BUF_REC_NR, multiple_buf_cont_enum_path,
			  ENUM_CONTAINER, true);
}

/* Map enumeration - single buffer fetch by iterator */
void me_single_buf(void)
{
	enumerate_generic(SINGLE_BUF_REC_NR, single_buf_map_enum_path,
			  ENUM_MAP, false);
	enumerate_generic(SINGLE_BUF_REC_NR, single_buf_map_enum_path,
			  ENUM_MAP, true);
}

/* Map enumeration - multiple buffer fetches by iterator */
void me_multiple_buf(void)
{
	enumerate_generic(MULTIPLE_BUF_REC_NR, multiple_buf_map_enum_path,
			  ENUM_MAP, false);
	enumerate_generic(MULTIPLE_BUF_REC_NR, multiple_buf_map_enum_path,
			  ENUM_MAP, true);
}

void test_iter_sensitivity(void)
{
	int			 rc;
	uint64_t		 container_id_in;
	uint64_t		 container_id_out;
	struct c2_fid		 fid_in;
	struct c2_fid		 fid_out;
	struct c2_uint128	 cob_fid_in;
	struct c2_uint128	 cob_fid_out;
	struct c2_dbenv		 cfm_dbenv;
	struct c2_db_tx		 cfm_dbtx;
	struct c2_addb_ctx	 cfm_addb_ctx;
	struct c2_cobfid_map	 cfm_map;
	struct c2_cobfid_map_iter cfm_iter;

	/* Reset any existing database */
	rc = c2_ut_db_reset(iter_test_map);
	C2_UT_ASSERT(rc == 0);

	/* Initialise the database with given path */
        rc = c2_dbenv_init(&cfm_dbenv, iter_test_map, 0);
	C2_UT_ASSERT(rc == 0);

	/* Initialize the map */
	rc = c2_cobfid_map_init(&cfm_map, &cfm_dbenv, &cfm_addb_ctx,
				"cfm_map_table");
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_init(&cfm_dbtx, &cfm_dbenv, 0);
	C2_UT_ASSERT(rc == 0);

	/* Populate initial key-values */
	container_id_in = 300;
	fid_in.f_container = 0;
	fid_in.f_key = 31;
	cob_fid_in.u_hi = 0;
	cob_fid_in.u_lo = 31;

	/* Add31 - add key with fid.f_key = 31 */
	rc = c2_cobfid_map_add(&cfm_map, container_id_in, fid_in, cob_fid_in,
			       &cfm_dbtx);
	C2_UT_ASSERT(rc == 0);

	fid_in.f_key = 52;
	cob_fid_in.u_lo = 52;

	/* Add52 - add key with fid.f_key = 52 */
	rc = c2_cobfid_map_add(&cfm_map, container_id_in, fid_in, cob_fid_in,
			       &cfm_dbtx);
	C2_UT_ASSERT(rc == 0);

	fid_in.f_key = 73;
	cob_fid_in.u_lo = 73;

	/* Add73 - add key with fid.f_key = 73 */
	rc = c2_cobfid_map_add(&cfm_map, container_id_in, fid_in, cob_fid_in,
			       &cfm_dbtx);
	C2_UT_ASSERT(rc == 0);

	/* Open the iterator */
	rc = c2_cobfid_map_container_enum(&cfm_map, container_id_in, &cfm_iter,
					  &cfm_dbtx);
	C2_UT_ASSERT(rc == 0);

	/* Get31 */
	rc = c2_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out); 
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fid_out.f_key == 31);
	C2_UT_ASSERT(cob_fid_out.u_lo == 31);

	fid_in.f_key = 94;
	cob_fid_in.u_lo = 94;

	/* Add94 - add key with fid.f_key = 94 */
	rc = c2_cobfid_map_add(&cfm_map, container_id_in, fid_in, cob_fid_in,
			       &cfm_dbtx);
	C2_UT_ASSERT(rc == 0);

	/* Get52 */
	rc = c2_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out); 
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fid_out.f_key == 52);
	C2_UT_ASSERT(cob_fid_out.u_lo == 52);

	fid_in.f_key = 20;
	cob_fid_in.u_lo = 20;

	/* Add20 - add key with fid.f_key = 20 */
	rc = c2_cobfid_map_add(&cfm_map, container_id_in, fid_in, cob_fid_in,
			       &cfm_dbtx);
	C2_UT_ASSERT(rc == 0);

	/* Get73 */
	rc = c2_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out); 
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fid_out.f_key == 73);
	C2_UT_ASSERT(cob_fid_out.u_lo == 73);

	fid_in.f_key = 87;
	cob_fid_in.u_lo = 87;

	/* Add87 - add key with fid.f_key = 87 */
	rc = c2_cobfid_map_add(&cfm_map, container_id_in, fid_in, cob_fid_in,
			       &cfm_dbtx);
	C2_UT_ASSERT(rc == 0);

	/* Get87 */
	rc = c2_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out); 
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fid_out.f_key == 87);
	C2_UT_ASSERT(cob_fid_out.u_lo == 87);

	/* Get94 */
	rc = c2_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out); 
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fid_out.f_key == 94);
	C2_UT_ASSERT(cob_fid_out.u_lo == 94);

	/* Close iterator */
	c2_cobfid_map_iter_fini(&cfm_iter);

	/* Open iterator */
	rc = c2_cobfid_map_container_enum(&cfm_map, container_id_in, &cfm_iter,
					  &cfm_dbtx);
	C2_UT_ASSERT(rc == 0);

	/* Get20 */
	rc = c2_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out); 
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fid_out.f_key == 20);
	C2_UT_ASSERT(cob_fid_out.u_lo == 20);

	/* Get31 */
	rc = c2_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out); 
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fid_out.f_key == 31);
	C2_UT_ASSERT(cob_fid_out.u_lo == 31);

	/* Get52 */
	rc = c2_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out); 
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fid_out.f_key == 52);
	C2_UT_ASSERT(cob_fid_out.u_lo == 52);

	/* Get73 */
	rc = c2_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out); 
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fid_out.f_key == 73);
	C2_UT_ASSERT(cob_fid_out.u_lo == 73);

	/* Get87 */
	rc = c2_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out); 
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fid_out.f_key == 87);
	C2_UT_ASSERT(cob_fid_out.u_lo == 87);

	/* Get94 */
	rc = c2_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out); 
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fid_out.f_key == 94);
	C2_UT_ASSERT(cob_fid_out.u_lo == 94);

	/* Iterator Empty */
	rc = c2_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out); 
	C2_UT_ASSERT(rc == -ENOENT);
	C2_UT_ASSERT(cfm_iter.cfmi_error == -ENOENT);

	c2_cobfid_map_fini(&cfm_map);
	c2_dbenv_fini(&cfm_dbenv);

	rc = c2_ut_db_reset(iter_test_map);
	C2_UT_ASSERT(rc == 0);

}

const struct c2_test_suite cfm_ut = {
	.ts_name = "cfm-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "cfm-container-enumerate-single-buffer", ce_single_buf },
		{ "cfm-container-enumerate-multiple-buffers", ce_multiple_buf },
		{ "cfm-map-enumerate-single-buffer", me_single_buf },
		{ "cfm-map-enumerate-multiple-buffers", me_multiple_buf },
		{ "cfm_iter_sensitivity", test_iter_sensitivity },
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
