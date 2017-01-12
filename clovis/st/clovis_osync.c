/* -*- C -*- */
/*
 * COPYRIGHT 2016 SEAGATE LLC
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF SEAGATE TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF SEAGATE TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF SEAGATE LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF SEAGATE'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A SEAGATE REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author:  Juan   Gonzalez <juan.gonzalez@seagate.com>
 *                   James  Morse    <james.s.morse@seagate.com>
 *                   Sining Wu       <sining.wu@seagate.com>
 * Original creation date: 11-11-2014
 */

/*
 * Clovis API system tests to check if Clovis API matches its
 * specifications.
 */

#include "clovis/clovis.h"
#include "clovis/st/clovis_st.h"
#include "clovis/st/clovis_st_misc.h"
#include "clovis/st/clovis_st_assert.h"

/* XXX playing around to try to debug */
#include "lib/trace.h"
#include "lib/memory.h"

struct m0_clovis_container clovis_st_osync_container;
struct m0_clovis_obj *obj_to_sync; /* best be a pointer due to st limitation */

#define PARGRP_UNIT_SIZE     (4096)
#define PARGRP_DATA_UNIT_NUM (2)
#define PARGRP_DATA_SIZE     (PARGRP_DATA_UNIT_NUM * PARGRP_UNIT_SIZE)

#define MAX_OPS (16)

/* Parity group aligned (in units)*/
enum {
	SMALL_OBJ_SIZE  = 1    * PARGRP_DATA_UNIT_NUM,
	MEDIUM_OBJ_SIZE = 100  * PARGRP_DATA_UNIT_NUM,
	LARGE_OBJ_SIZE  = 2000 * PARGRP_DATA_UNIT_NUM
};

static int create_obj(struct m0_uint128 *oid)
{
	int                   rc = 0;
	struct m0_clovis_op  *ops[1] = {NULL};
	struct m0_clovis_obj  obj;
	struct m0_uint128     id;

	/* Make sure everything in 'obj' is clean*/
	memset(&obj, 0, sizeof(obj));

	clovis_oid_get(&id);
	clovis_st_obj_init(&obj, &clovis_st_osync_container.co_realm, &id);
	clovis_st_entity_create(&obj.ob_entity, &ops[0]);
	if (ops[0] == NULL)
		return -ENOENT;

	clovis_st_op_launch(ops, ARRAY_SIZE(ops));

	rc = clovis_st_op_wait(
		ops[0], M0_BITS(M0_CLOVIS_OS_FAILED, M0_CLOVIS_OS_STABLE),
		m0_time_from_now(3,0));

	clovis_st_op_fini(ops[0]);
	clovis_st_op_free(ops[0]);
	clovis_st_obj_fini(&obj);

	*oid = id;
	return rc;
}

/*
 * We issue 'nr_ops' of WRITES in one go.
 * stride: in units
 */
static int write_obj(int start, int stride, int nr_ops)
{
	int                    i;
	int                    rc = 0;
	struct m0_clovis_op  **ops_w;
	struct m0_indexvec    *ext_w;
	struct m0_bufvec      *data_w;
	struct m0_bufvec      *attr_w;

	M0_CLOVIS_THREAD_ENTER;

	if (nr_ops > MAX_OPS)
		return -EINVAL;

	/* Setup bufvec, indexvec and ops for WRITEs */
	MEM_ALLOC_ARR(ops_w, nr_ops);
	MEM_ALLOC_ARR(ext_w, nr_ops);
	MEM_ALLOC_ARR(data_w, nr_ops);
	MEM_ALLOC_ARR(attr_w, nr_ops);
	if (ops_w == NULL || ext_w == NULL || data_w == NULL || attr_w == NULL)
		goto CLEANUP;

	for (i = 0; i < nr_ops; i++) {
		if (m0_indexvec_alloc(&ext_w[i], 1) ||
		    m0_bufvec_alloc(&data_w[i], 1, 4096 * stride) ||
		    m0_bufvec_alloc(&attr_w[i], 1, 1))
		{
			rc = -ENOMEM;
			goto CLEANUP;
		}

		ext_w[i].iv_index[0] = 4096 * (start +  i * stride);
		ext_w[i].iv_vec.v_count[0] = 4096 * stride;
		attr_w[i].ov_vec.v_count[0] = 0;
	}

	/* Create and launch write requests */
	for (i = 0; i < nr_ops; i++) {
		//clovis_obj_init(obj_to_sync, &clovis_st_write_container.co_realm, &id);
		ops_w[i] = NULL;
		clovis_st_obj_op(obj_to_sync, M0_CLOVIS_OC_WRITE,
			      &ext_w[i], &data_w[i], &attr_w[i], 0, &ops_w[i]);
		if (ops_w[i] == NULL)
			break;
	}
	if (i == 0) goto CLEANUP;

	clovis_st_op_launch(ops_w, nr_ops);

	/* Wait for write to finish */
	for (i = 0; i < nr_ops; i++) {
		rc = clovis_st_op_wait(ops_w[i],
			M0_BITS(M0_CLOVIS_OS_FAILED,
				M0_CLOVIS_OS_STABLE),
			M0_TIME_NEVER);

		clovis_st_op_fini(ops_w[i]);
		clovis_st_op_free(ops_w[i]);
	}


CLEANUP:

	for (i = 0; i < nr_ops; i++) {
		if (ext_w != NULL && ext_w[i].iv_vec.v_nr != 0)
			m0_indexvec_free(&ext_w[i]);
		if (data_w != NULL && data_w[i].ov_buf != NULL)
			m0_bufvec_free(&data_w[i]);
		if (attr_w != NULL && attr_w[i].ov_buf != NULL)
			m0_bufvec_free(&attr_w[i]);
	}

	if (ops_w != NULL) mem_free(ops_w);
	if (ext_w != NULL) mem_free(ext_w);
	if (data_w != NULL) mem_free(data_w);
	if (attr_w != NULL) mem_free(attr_w);

	return rc;
}

/**
 * sync data for each write operation.
 */
static void osync_after_each_write(void)
{
	int               i;
	int               rc;
	int               start;
	int               stride;
	struct m0_uint128 oid;

	MEM_ALLOC_PTR(obj_to_sync);
	CLOVIS_ST_ASSERT_FATAL(obj_to_sync != NULL);

	/* Create an object */
	rc = create_obj(&oid);
	CLOVIS_ST_ASSERT_FATAL(rc == 0);

	/* Init obj */
	clovis_st_obj_init(obj_to_sync,
			   &clovis_st_osync_container.co_realm, &oid);

	/* Write multiple times and sync after each write*/
	start  = 0;
	stride = PARGRP_DATA_UNIT_NUM;
	for (i = 0; i < 2; i++) {
		rc = write_obj(start, stride, 1);
		CLOVIS_ST_ASSERT_FATAL(rc == 0)

		rc = m0_clovis_obj_sync(obj_to_sync);
		CLOVIS_ST_ASSERT_FATAL(rc == 0);

		start += stride;
		stride *= 2;
	}

	clovis_st_obj_fini(obj_to_sync);
	mem_free(obj_to_sync);
}

/**
 * Only sync data after all write ops are done.
 */
static void osync_after_writes(void)
{
	int               i;
	int               rc;
	int               start;
	int               stride;
	struct m0_uint128 oid;

	MEM_ALLOC_PTR(obj_to_sync);
	CLOVIS_ST_ASSERT_FATAL(obj_to_sync != NULL);

	/* Create an object */
	rc = create_obj(&oid);
	CLOVIS_ST_ASSERT_FATAL(rc == 0);

	/* Init obj */
	clovis_st_obj_init(obj_to_sync,
			   &clovis_st_osync_container.co_realm, &oid);

	/* Write multiple times and sync after each write*/
	start  = 0;
	stride = PARGRP_DATA_UNIT_NUM;
	for (i = 0; i < 2; i++) {
		rc = write_obj(start, stride, 1);
		CLOVIS_ST_ASSERT_FATAL(rc == 0);

		start += stride;
		stride *= 2;
	}

	rc = m0_clovis_obj_sync(obj_to_sync);
	CLOVIS_ST_ASSERT_FATAL (rc == 0)

	clovis_st_obj_fini(obj_to_sync);
	mem_free(obj_to_sync);
}

/* Initialises the Clovis environment.*/
static int clovis_st_osync_init(void)
{
	int rc = 0;

	/*
	 * Retrieve the uber realm. We don't need to open this,
	 * as realms are not actually implemented yet
	 */
	clovis_st_container_init(&clovis_st_osync_container,
			      NULL, &M0_CLOVIS_UBER_REALM,
			      clovis_st_get_instance());
	rc = clovis_st_osync_container.co_realm.re_entity.en_sm.sm_rc;

	if (rc != 0)
		console_printf("Failed to open uber realm\n");

	return rc;
}

/* Finalises the Clovis environment.*/
static int clovis_st_osync_fini(void)
{
	return 0;
}

struct clovis_st_suite st_suite_clovis_osync = {
	.ss_name = "clovis_osync_st",
	.ss_init = clovis_st_osync_init,
	.ss_fini = clovis_st_osync_fini,
	.ss_tests = {
		{ "osync_after_each_write", &osync_after_each_write},
		{ "osync_after_writes",     &osync_after_writes},
		{ NULL, NULL }
	}
};

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
