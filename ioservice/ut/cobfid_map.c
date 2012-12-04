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

static const struct m0_addb_ctx_type cfm_ut_addb_ctx_type = {
	.act_name = "cobfid_map_ut"
};

/* DB paths for various databases */
static const char single_buf_cont_enum_path[] = "./cfm_map_single_buf_ce";
static const char multiple_buf_cont_enum_path[] = "./cfm_map_multiple_buf_ce";
static const char single_buf_map_enum_path[] = "./cfm_map_single_buf_me";
static const char multiple_buf_map_enum_path[] = "./cfm_map_multiple_buf_me";
static const char iter_test_map[] = "./cfm_map_iter_test";
static const char concurrency_test_map[] = "./cfm_map_concurrency_test";

/*
   Generic enumeration routine, used by all the tests.
   The parameters are :
   rec_total: Total number of records to be inserted and enumerated
   map_path : Database path used for the addition and enumeration
   etype    : Enumeration type (either map or container)
   check_persistence : Flag to check the database persistence
 */
static void enumerate_generic(const int rec_total, const char *map_path,
			      const int etype, const bool check_persistence)
{
	int			 i;
	int			 j;
	int			 rc;
	int			 rec_nr;
	uint64_t		 container_id_in;
	uint64_t		 container_id_out;
	uint64_t		*cid_in;
	uint64_t		*cid_out;
	struct m0_fid		*fid_in;
	struct m0_fid		*fid_out;
	struct m0_uint128	*cob_fid_in;
	struct m0_uint128	*cob_fid_out;
	struct m0_dbenv		 cfm_dbenv;
	struct m0_addb_ctx	 cfm_addb_ctx;
	struct m0_cobfid_map	 cfm_map;
	struct m0_cobfid_map_iter cfm_iter;

	/* Reset any existing database */
	rc = m0_ut_db_reset(map_path);
	M0_UT_ASSERT(rc == 0);

	/* Initialise the database with given path */
        rc = m0_dbenv_init(&cfm_dbenv, map_path, 0);
	M0_UT_ASSERT(rc == 0);

	m0_addb_ctx_init(&cfm_addb_ctx, &cfm_ut_addb_ctx_type,
			 &m0_addb_global_ctx);
	/* Initialize the map */
	rc = m0_cobfid_map_init(&cfm_map, &cfm_dbenv, &cfm_addb_ctx,
				"cfm_map_table");
	M0_UT_ASSERT(rc == 0);

	/* Allocate the input and output key-value arrays with total number
	   of records.
	   - Input arrays are populated during insertion operation.
	   - Corresponding output key-value arrays are populated
	     during enumeration operation.
	   - Input and Output arrays are compared at the end for equality */
	M0_ALLOC_ARR(fid_in, rec_total);
	M0_UT_ASSERT(fid_in != NULL);

	M0_ALLOC_ARR(cob_fid_in, rec_total);
	M0_UT_ASSERT(cob_fid_in != NULL);

	M0_ALLOC_ARR(fid_out, rec_total);
	M0_UT_ASSERT(fid_out != NULL);

	M0_ALLOC_ARR(cob_fid_out, rec_total);
	M0_UT_ASSERT(cob_fid_out != NULL);

	/* Allocate the container arrays only for map enumeration. Use constant
	   container id for container enumeration*/
	if (etype == ENUM_MAP) {
		M0_ALLOC_ARR(cid_in, rec_total);
		M0_UT_ASSERT(cid_in != NULL);

		M0_ALLOC_ARR(cid_out, rec_total);
		M0_UT_ASSERT(cid_out != NULL);
	} else {
		cid_in = NULL;
		cid_out = NULL;
		container_id_in = 200;
	}

	j = rec_total - 1;

	/* Fill in the database with varying fid values in
	   decreasing order of fid key. This is done on purpose to test the
	   ordering property where enumeration should be done in increasing
	   order of the fid key.*/
	for (i = 0; i < rec_total; i++) {
		/* Populate some random data */
		m0_fid_set(&fid_in[i], 0, j);
		cob_fid_in[i].u_hi = 333;
		cob_fid_in[i].u_lo = j;


		if (etype == ENUM_MAP) {
			cid_in[i] = j;
			rc = m0_cobfid_map_add(&cfm_map, cid_in[i],
					       fid_in[i], cob_fid_in[i]);
		} else
			rc = m0_cobfid_map_add(&cfm_map, container_id_in,
					       fid_in[i], cob_fid_in[i]);

		M0_UT_ASSERT(rc == 0);
		j--;
	}

	/* Finalise the DB environment, transaction and cobfid_map,
	   and reinitialize to check database persistence */
	if (check_persistence) {
		m0_cobfid_map_fini(&cfm_map);
		m0_dbenv_fini(&cfm_dbenv);

		/* Initialise the database with given path */
		rc = m0_dbenv_init(&cfm_dbenv, map_path, 0);
		M0_UT_ASSERT(rc == 0);

		/* Initialize the map */
		rc = m0_cobfid_map_init(&cfm_map, &cfm_dbenv, &cfm_addb_ctx,
					"cfm_map_table");
		M0_UT_ASSERT(rc == 0);

	}

	rec_nr = 0;
	/* Container enumeration */
	if (etype == ENUM_CONTAINER) {
		rc = m0_cobfid_map_container_enum(&cfm_map, container_id_in,
						  &cfm_iter);
		M0_UT_ASSERT(rc == 0);
		while ((rc = m0_cobfid_map_iter_next(&cfm_iter,
						&container_id_out,
						&fid_out[rec_nr],
						&cob_fid_out[rec_nr])) == 0) {
			rec_nr++;
		}
		M0_UT_ASSERT(cfm_iter.cfmi_error == -ENOENT);
		M0_UT_ASSERT(rc == -ENOENT);
	} else if (etype == ENUM_MAP) { /* Map enumeration */
		rc = m0_cobfid_map_enum(&cfm_map, &cfm_iter);
		M0_UT_ASSERT(rc == 0);
		while ((rc = m0_cobfid_map_iter_next(&cfm_iter,
						&cid_out[rec_nr],
						&fid_out[rec_nr],
						&cob_fid_out[rec_nr])) == 0) {
			rec_nr++;
		}
		M0_UT_ASSERT(cfm_iter.cfmi_error == -ENOENT);
		M0_UT_ASSERT(rc == -ENOENT);
	}

	m0_cobfid_map_iter_fini(&cfm_iter);

	/* Check if number of records enumerated is same as number of records
	   inserted */
	M0_UT_ASSERT(rec_nr == rec_total);

	/* Check if the fid and cob input arrays are exact reverse of their
	   corresponding out counterparts. Also check the cid array in case
	   of map enumeration*/
	for (i = 0; i < rec_total; i++) {
		M0_UT_ASSERT(m0_fid_eq(&fid_in[i],
				       &fid_out[rec_total - 1 - i]));
		M0_UT_ASSERT(m0_uint128_eq(&cob_fid_in[i],
					   &cob_fid_out[rec_total - 1 - i]));
		if (etype == ENUM_MAP) {
			M0_UT_ASSERT(M0_3WAY(cid_in[i],
					     cid_out[rec_total - 1 - i] == 0));
		}
	}

	m0_free(fid_in);
	m0_free(cob_fid_in);
	m0_free(fid_out);
	m0_free(cob_fid_out);

	if (etype == ENUM_MAP) {
		m0_free(cid_in);
		m0_free(cid_out);
	}

	m0_cobfid_map_fini(&cfm_map);
	m0_dbenv_fini(&cfm_dbenv);

	rc = m0_ut_db_reset(map_path);
	M0_UT_ASSERT(rc == 0);
}

/* Container enumeration - single buffer fetch by iterator */
static void ce_single_buf(void)
{
	/* Do not check database persistence */
	enumerate_generic(SINGLE_BUF_REC_NR, single_buf_cont_enum_path,
			  ENUM_CONTAINER, false);
	/* Check database persistence */
	enumerate_generic(SINGLE_BUF_REC_NR, single_buf_cont_enum_path,
			  ENUM_CONTAINER, true);
}

/* Container enumeration - multiple buffer fetches by iterator */
static void ce_multiple_buf(void)
{
	/* Do not check database persistence */
	enumerate_generic(MULTIPLE_BUF_REC_NR, multiple_buf_cont_enum_path,
			  ENUM_CONTAINER, false);
	/* Check database persistence */
	enumerate_generic(MULTIPLE_BUF_REC_NR, multiple_buf_cont_enum_path,
			  ENUM_CONTAINER, true);
}

/* Map enumeration - single buffer fetch by iterator */
static void me_single_buf(void)
{
	/* Do not check database persistence */
	enumerate_generic(SINGLE_BUF_REC_NR, single_buf_map_enum_path,
			  ENUM_MAP, false);
	/* Check database persistence */
	enumerate_generic(SINGLE_BUF_REC_NR, single_buf_map_enum_path,
			  ENUM_MAP, true);
}

/* Map enumeration - multiple buffer fetches by iterator */
static void me_multiple_buf(void)
{
	/* Do not check database persistence */
	enumerate_generic(MULTIPLE_BUF_REC_NR, multiple_buf_map_enum_path,
			  ENUM_MAP, false);
	/* Check database persistence */
	enumerate_generic(MULTIPLE_BUF_REC_NR, multiple_buf_map_enum_path,
			  ENUM_MAP, true);
}

/* Iterator sensitivity - test to ensure that an iterator behaves correctly
   when records are inserted during its use. */
static void test_iter_sensitivity(void)
{
	int			 rc;
	uint64_t		 container_id_in;
	uint64_t		 container_id_out;
	struct m0_fid		 fid_in;
	struct m0_fid		 fid_out;
	struct m0_uint128	 cob_fid_in;
	struct m0_uint128	 cob_fid_out;
	struct m0_dbenv		 cfm_dbenv;
	struct m0_addb_ctx	 cfm_addb_ctx;
	struct m0_cobfid_map	 cfm_map;
	struct m0_cobfid_map_iter cfm_iter;

	/* Reset any existing database */
	rc = m0_ut_db_reset(iter_test_map);
	M0_UT_ASSERT(rc == 0);

	/* Initialise the database with given path */
        rc = m0_dbenv_init(&cfm_dbenv, iter_test_map, 0);
	M0_UT_ASSERT(rc == 0);

	/* Initialize the map */
	rc = m0_cobfid_map_init(&cfm_map, &cfm_dbenv, &cfm_addb_ctx,
				"cfm_map_table");
	M0_UT_ASSERT(rc == 0);

	/* Populate initial key-values */
	container_id_in = 300;
	m0_fid_set(&fid_in, 0, 31);
	cob_fid_in.u_hi = 0;
	cob_fid_in.u_lo = 31;

	/* Add31 - add key with fid.f_key = 31 */
	rc = m0_cobfid_map_add(&cfm_map, container_id_in, fid_in, cob_fid_in);
	M0_UT_ASSERT(rc == 0);

	fid_in.f_key = 52;
	cob_fid_in.u_lo = 52;

	/* Add52 - add key with fid.f_key = 52 */
	rc = m0_cobfid_map_add(&cfm_map, container_id_in, fid_in, cob_fid_in);
	M0_UT_ASSERT(rc == 0);

	fid_in.f_key = 73;
	cob_fid_in.u_lo = 73;

	/* Add73 - add key with fid.f_key = 73 */
	rc = m0_cobfid_map_add(&cfm_map, container_id_in, fid_in, cob_fid_in);
	M0_UT_ASSERT(rc == 0);

	/* Open the iterator */
	rc = m0_cobfid_map_container_enum(&cfm_map, container_id_in, &cfm_iter);
	M0_UT_ASSERT(rc == 0);

	/* Get31 */
	rc = m0_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(fid_out.f_key == 31);
	M0_UT_ASSERT(cob_fid_out.u_lo == 31);

	fid_in.f_key = 94;
	cob_fid_in.u_lo = 94;

	/* Add94 - add key with fid.f_key = 94 */
	rc = m0_cobfid_map_add(&cfm_map, container_id_in, fid_in, cob_fid_in);
	M0_UT_ASSERT(rc == 0);

	/* Get52 */
	rc = m0_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(fid_out.f_key == 52);
	M0_UT_ASSERT(cob_fid_out.u_lo == 52);

	fid_in.f_key = 20;
	cob_fid_in.u_lo = 20;

	/* Add20 - add key with fid.f_key = 20 */
	rc = m0_cobfid_map_add(&cfm_map, container_id_in, fid_in, cob_fid_in);
	M0_UT_ASSERT(rc == 0);

	/* Get73 */
	rc = m0_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(fid_out.f_key == 73);
	M0_UT_ASSERT(cob_fid_out.u_lo == 73);

	fid_in.f_key = 87;
	cob_fid_in.u_lo = 87;

	/* Add87 - add key with fid.f_key = 87 */
	rc = m0_cobfid_map_add(&cfm_map, container_id_in, fid_in, cob_fid_in);
	M0_UT_ASSERT(rc == 0);

	/* Get87 */
	rc = m0_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(fid_out.f_key == 87);
	M0_UT_ASSERT(cob_fid_out.u_lo == 87);

	/* Get94 */
	rc = m0_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(fid_out.f_key == 94);
	M0_UT_ASSERT(cob_fid_out.u_lo == 94);

	/* Close iterator */
	m0_cobfid_map_iter_fini(&cfm_iter);

	/* Open iterator */
	rc = m0_cobfid_map_container_enum(&cfm_map, container_id_in, &cfm_iter);
	M0_UT_ASSERT(rc == 0);

	/* Get20 */
	rc = m0_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(fid_out.f_key == 20);
	M0_UT_ASSERT(cob_fid_out.u_lo == 20);

	/* Get31 */
	rc = m0_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(fid_out.f_key == 31);
	M0_UT_ASSERT(cob_fid_out.u_lo == 31);

	/* Get52 */
	rc = m0_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(fid_out.f_key == 52);
	M0_UT_ASSERT(cob_fid_out.u_lo == 52);

	/* Get73 */
	rc = m0_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(fid_out.f_key == 73);
	M0_UT_ASSERT(cob_fid_out.u_lo == 73);

	/* Get87 */
	rc = m0_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(fid_out.f_key == 87);
	M0_UT_ASSERT(cob_fid_out.u_lo == 87);

	/* Get94 */
	rc = m0_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(fid_out.f_key == 94);
	M0_UT_ASSERT(cob_fid_out.u_lo == 94);

	/* Iterator Empty */
	rc = m0_cobfid_map_iter_next(&cfm_iter, &container_id_out, &fid_out,
				     &cob_fid_out);
	M0_UT_ASSERT(rc == -ENOENT);
	M0_UT_ASSERT(cfm_iter.cfmi_error == -ENOENT);

	/* Close iterator */
	m0_cobfid_map_iter_fini(&cfm_iter);

	m0_cobfid_map_fini(&cfm_map);
	m0_dbenv_fini(&cfm_dbenv);

	rc = m0_ut_db_reset(iter_test_map);
	M0_UT_ASSERT(rc == 0);

}

/* Number of threads that will concurrently access a cobfid_map */
enum {
	CFM_THREAD_NR = 41,
};

static struct m0_mutex	    cfm_global_mutex;
static struct m0_dbenv	    cfm_global_dbenv;
static struct m0_cobfid_map cfm_global_map;
static struct m0_addb_ctx   cfm_global_addb_ctx;
static struct m0_fid	   *fid_in[CFM_THREAD_NR];
static struct m0_fid	   *fid_out[CFM_THREAD_NR];
static struct m0_uint128   *cob_fid_in[CFM_THREAD_NR];
static struct m0_uint128   *cob_fid_out[CFM_THREAD_NR];

/* Container id is function of thread id, implemented for uniqueness */
static uint64_t get_cid(const int tid)
{
	return 954 + tid;
}

/* Function to add and enumerate a container */
static void cfm_op(const int tid)
{
	int			 i;
	int			 j;
	int			 rc;
	int			 rec_nr;
	int			 rec_total;
        uint64_t                 container_id_in;
        uint64_t                 container_id_out;
        struct m0_cobfid_map_iter cfm_iter;

	/* Use multiple fetches for iterator */
	rec_total = MULTIPLE_BUF_REC_NR;

	/* Allocate key-value input-output arrays only for this thread id */
	M0_ALLOC_ARR(fid_in[tid], rec_total);
        M0_UT_ASSERT(fid_in[tid] != NULL);

        M0_ALLOC_ARR(cob_fid_in[tid], rec_total);
        M0_UT_ASSERT(cob_fid_in[tid] != NULL);

        M0_ALLOC_ARR(fid_out[tid], rec_total);
        M0_UT_ASSERT(fid_out[tid] != NULL);

        M0_ALLOC_ARR(cob_fid_out[tid], rec_total);
        M0_UT_ASSERT(cob_fid_out[tid] != NULL);

	/* Container id is function of thread id */
	container_id_in = get_cid(tid);
	j = rec_total - 1;

        /* Fill in the database with varying fid values in
           decreasing order of fid key. This is done on purpose to test the
           ordering property where enumeration should be done in increasing
           order of the fid key.*/
        for (i = 0; i < rec_total; i++) {
                /* Populate some random data */
                m0_fid_set(&fid_in[tid][i], 0, j);
                cob_fid_in[tid][i].u_hi = 3212;
                cob_fid_in[tid][i].u_lo = j;

		/* Add to map using serialization */
		m0_mutex_lock(&cfm_global_mutex);

		rc = m0_cobfid_map_add(&cfm_global_map, container_id_in,
				       fid_in[tid][i], cob_fid_in[tid][i]);
                M0_UT_ASSERT(rc == 0);

		m0_mutex_unlock(&cfm_global_mutex);

                j--;
        }

        rec_nr = 0;

	/* Container enumeration for this thread */
	m0_mutex_lock(&cfm_global_mutex);

	rc = m0_cobfid_map_container_enum(&cfm_global_map, container_id_in,
					  &cfm_iter);
	M0_UT_ASSERT(rc == 0);

	while ((rc = m0_cobfid_map_iter_next(&cfm_iter, &container_id_out,
					     &fid_out[tid][rec_nr],
					     &cob_fid_out[tid][rec_nr])) == 0) {
		rec_nr++;
	}

	m0_cobfid_map_iter_fini(&cfm_iter);
	m0_mutex_unlock(&cfm_global_mutex);

	/* Check if number of records enumerated is same as number of records
	   inserted */
        M0_UT_ASSERT(rec_nr == rec_total);

        /* Check if the fid and cob input arrays are exact reverse of their
           corresponding out counterparts. */
        for (i = 0; i < rec_total; i++) {
                M0_UT_ASSERT(m0_fid_eq(&fid_in[tid][i],
                                       &fid_out[tid][rec_total - 1 - i]));
                M0_UT_ASSERT(m0_uint128_eq(&cob_fid_in[tid][i],
                             &cob_fid_out[tid][rec_total - 1 - i]));
        }

	/* Free input arrays since they are not required henceforth.
	   Output arrays are not freed as they are required for validation */
        m0_free(fid_in[tid]);
        m0_free(cob_fid_in[tid]);
}

/* Test concurrent operation across multiple threads for a cobfid_map */
static void test_cfm_concurrency(void)
{
	int			  i;
	int			  rc;
	int			  rec_nr;
	int			  tid;
	int			  tid_rec_nr;
	struct m0_thread	 *cfm_thread;
	struct m0_cobfid_map_iter cfm_iter;
	uint64_t		  cid;
	struct m0_uint128         cob_fid;
	struct m0_fid             fid;

	/* Reset any existing database */
	rc = m0_ut_db_reset(concurrency_test_map);
	M0_UT_ASSERT(rc == 0);

        /* Initialise the database with given path */
        rc = m0_dbenv_init(&cfm_global_dbenv, concurrency_test_map, 0);
        M0_UT_ASSERT(rc == 0);

	m0_addb_ctx_init(&cfm_global_addb_ctx, &cfm_ut_addb_ctx_type,
			 &m0_addb_global_ctx);

	/* Initialize the map */
	rc = m0_cobfid_map_init(&cfm_global_map, &cfm_global_dbenv,
				&cfm_global_addb_ctx, "cfm_map_table");
	M0_UT_ASSERT(rc == 0);

	m0_mutex_init(&cfm_global_mutex);

	M0_ALLOC_ARR(cfm_thread, CFM_THREAD_NR);
	M0_UT_ASSERT(cfm_thread != NULL);

	/* Spawn threads with even thread id,
	   which will independently add and enumerate */
	for (i = 0; i < CFM_THREAD_NR; i = i + 2) {
		rc = M0_THREAD_INIT(&cfm_thread[i], int, NULL, &cfm_op, i,
				    "cfm_thread_%d", i);
		M0_UT_ASSERT(rc == 0);
	}

	/* Spawn threads with odd thread id,
	   which will independently add and enumerate */
	for (i = 1; i < CFM_THREAD_NR; i = i + 2) {
		rc = M0_THREAD_INIT(&cfm_thread[i], int, NULL, &cfm_op, i,
				    "cfm_thread_%d", i);
		M0_UT_ASSERT(rc == 0);
	}

	for (i = 0; i < CFM_THREAD_NR; ++i) {
		m0_thread_join(&cfm_thread[i]);
	}

	m0_free(cfm_thread);
	m0_mutex_fini(&cfm_global_mutex);

	/* Map enumeration for records inserted by above threads */
	rc = m0_cobfid_map_enum(&cfm_global_map, &cfm_iter);
	M0_UT_ASSERT(rc == 0);

	rec_nr = 0;
	tid = 0;
	tid_rec_nr = 0;

	while ((rc = m0_cobfid_map_iter_next(&cfm_iter, &cid, &fid,
					     &cob_fid)) == 0) {
		/* Check the ordering of enumeration. The data from all the
		   threads has to be in correct order. */
                M0_UT_ASSERT(m0_fid_eq(&fid_out[tid][tid_rec_nr], &fid));
                M0_UT_ASSERT(m0_uint128_eq(&cob_fid_out[tid][tid_rec_nr],
					   &cob_fid));
		M0_UT_ASSERT(M0_3WAY(cid, get_cid(tid) == 0));

		rec_nr++;
		tid_rec_nr++;

		if (tid_rec_nr == MULTIPLE_BUF_REC_NR) {
			/* Free output arrays as they are not required after
			   this point */
			m0_free(fid_out[tid]);
			m0_free(cob_fid_out[tid]);

			tid++;
			tid_rec_nr = 0;
		}
	}

	/* Check end of the iterator */
	M0_UT_ASSERT(cfm_iter.cfmi_error == -ENOENT);
	M0_UT_ASSERT(rc == -ENOENT);

        m0_cobfid_map_iter_fini(&cfm_iter);

        /* Check if number of records enumerated is same as number of records
           inserted */
        M0_UT_ASSERT(rec_nr == CFM_THREAD_NR * MULTIPLE_BUF_REC_NR);

	/* Cleanup */
	m0_cobfid_map_fini(&cfm_global_map);
	m0_dbenv_fini(&cfm_global_dbenv);

	rc = m0_ut_db_reset(concurrency_test_map);
	M0_UT_ASSERT(rc == 0);
}

const struct m0_test_suite cfm_ut = {
	.ts_name = "cfm-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "cfm-container-enumerate-single-buffer", ce_single_buf },
		{ "cfm-container-enumerate-multiple-buffers", ce_multiple_buf },
		{ "cfm-map-enumerate-single-buffer", me_single_buf },
		{ "cfm-map-enumerate-multiple-buffers", me_multiple_buf },
		{ "cfm-iter-sensitivity", test_iter_sensitivity },
		{ "cfm-concurrency", test_cfm_concurrency },
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
