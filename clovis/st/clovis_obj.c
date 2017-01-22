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
 * Original creation date: 17-Sep-2014
 */

/*
 * Clovis API system tests to check if Clovis API matches its
 * specifications.
 */

#include "clovis/clovis.h"
#include "clovis/st/clovis_st.h"
#include "clovis/st/clovis_st_misc.h"
#include "clovis/st/clovis_st_assert.h"

#include "lib/trace.h" /* M0_LOG() */

#include "lib/memory.h"
#include "lib/types.h"
#include "lib/errno.h" /* ENOENT */

#ifndef __KERNEL__
#include <stdlib.h>
#include <unistd.h>
#else
#include <linux/delay.h>
#endif

static struct m0_uint128 test_id;
struct m0_clovis_container clovis_st_obj_container;

/**
 * Creates an object.
 *
 * @remark This test does not call op_wait().
 */
static void obj_create_simple(void)
{
	struct m0_clovis_op    *ops[] = {NULL};
	struct m0_clovis_obj   *obj;
	struct m0_uint128       id;
	int                     rc;

	MEM_ALLOC_PTR(obj);
	CLOVIS_ST_ASSERT_FATAL(obj != NULL);

	/* Initialise ids. */
	clovis_oid_get(&id);

	clovis_st_obj_init(obj, &clovis_st_obj_container.co_realm, &id);
	rc = clovis_st_entity_create(&obj->ob_entity, &ops[0]);
	CLOVIS_ST_ASSERT_FATAL(rc == 0);
	CLOVIS_ST_ASSERT_FATAL(ops[0] != NULL);
	CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_rc == 0);

	clovis_st_op_launch(ops, ARRAY_SIZE(ops));

	rc = clovis_st_op_wait(ops[0], M0_BITS(M0_CLOVIS_OS_FAILED,
					       M0_CLOVIS_OS_STABLE),
				m0_time_from_now(5,0));
	CLOVIS_ST_ASSERT_FATAL(rc == 0);
	CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_state == M0_CLOVIS_OS_STABLE);

	clovis_st_op_fini(ops[0]);
	clovis_st_op_free(ops[0]);
	clovis_st_entity_fini(&obj->ob_entity);
	mem_free(obj);
}

/**
 * Tests a double object creation. Creating the same object twice must result
 * in the following scenario: one succeeds; the other one fails;
 * the object gets created.
 *
 * @remarks This test uses two different objects, but the same obj ID.
 */
static void obj_create_double_same_id(void)
{
	struct m0_clovis_op            *ops[] = {NULL, NULL};
	struct m0_clovis_obj          **obj;
	struct m0_uint128               id;
	int                             rc = 0;

	MEM_ALLOC_ARR(obj, 2);
	CLOVIS_ST_ASSERT_FATAL(obj != NULL);
	MEM_ALLOC_PTR(obj[0]);
	MEM_ALLOC_PTR(obj[1]);

	/* Initialise ids. */
	clovis_oid_get(&id);

	clovis_st_obj_init(obj[0], &clovis_st_obj_container.co_realm, &id);
	clovis_st_obj_init(obj[1], &clovis_st_obj_container.co_realm, &id);

	clovis_st_entity_create(&obj[0]->ob_entity, &ops[0]);
	CLOVIS_ST_ASSERT_FATAL(ops[0] != NULL);
	CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_rc == 0);
	clovis_st_entity_create(&obj[1]->ob_entity, &ops[1]);
	CLOVIS_ST_ASSERT_FATAL(ops[1] != NULL);
	CLOVIS_ST_ASSERT_FATAL(ops[1]->op_sm.sm_rc == 0);

	clovis_st_op_launch(ops, ARRAY_SIZE(ops));

	rc = clovis_st_op_wait(ops[0], M0_BITS(M0_CLOVIS_OS_FAILED,
					       M0_CLOVIS_OS_STABLE),
			       m0_time_from_now(5,0));
	CLOVIS_ST_ASSERT_FATAL(rc == 0);
	rc = clovis_st_op_wait(ops[1], M0_BITS(M0_CLOVIS_OS_FAILED,
					       M0_CLOVIS_OS_STABLE),
			       m0_time_from_now(5,0));
	CLOVIS_ST_ASSERT_FATAL(rc == 0);

	/* One succeeds, the other fails. */
	CLOVIS_ST_ASSERT_FATAL((ops[0]->op_sm.sm_state == M0_CLOVIS_OS_STABLE &&
				ops[1]->op_sm.sm_state == M0_CLOVIS_OS_FAILED) ||
			       (ops[0]->op_sm.sm_state == M0_CLOVIS_OS_FAILED &&
				ops[1]->op_sm.sm_state == M0_CLOVIS_OS_STABLE));
	clovis_st_op_fini(ops[0]);
	clovis_st_op_fini(ops[1]);
	clovis_st_op_free(ops[0]);
	clovis_st_op_free(ops[1]);

	clovis_st_entity_fini(&obj[0]->ob_entity);
	clovis_st_entity_fini(&obj[1]->ob_entity);

	mem_free(obj[1]);
	mem_free(obj[0]);
	mem_free(obj);
}

/**
 * Uses clovis to create multiple objects. All the operations are expected
 * to complete.
 *
 * @remarks Every object and operation used by this test are correctly
 * finalised and released.
 */
static void obj_create_multiple_objects(void)
{
#define CREATE_MULTIPLE_N_OBJS 20
	uint32_t                i;
	struct m0_clovis_op    *ops[CREATE_MULTIPLE_N_OBJS] = {NULL};
	struct m0_clovis_obj  **objs = NULL;
	struct m0_uint128       id[CREATE_MULTIPLE_N_OBJS];
	int                     rc;

	MEM_ALLOC_ARR(objs, CREATE_MULTIPLE_N_OBJS);
	for (i = 0; i < CREATE_MULTIPLE_N_OBJS; ++i) {
		MEM_ALLOC_PTR(objs[i]);
		CLOVIS_ST_ASSERT_FATAL(objs[i] != NULL);
	}

	/* Initialise ids. */
	for (i = 0; i < CREATE_MULTIPLE_N_OBJS; ++i) {
		clovis_oid_get(id + i);
	}

	/* Create different objects. */
	for (i = 0; i < CREATE_MULTIPLE_N_OBJS; ++i) {
		clovis_st_obj_init(objs[i], &clovis_st_obj_container.co_realm,
				   &id[i]);
		rc = clovis_st_entity_create(&objs[i]->ob_entity, &ops[i]);
		CLOVIS_ST_ASSERT_FATAL(rc == 0);
		CLOVIS_ST_ASSERT_FATAL(ops[i] != NULL);
		CLOVIS_ST_ASSERT_FATAL(ops[i]->op_sm.sm_rc == 0);
	}

	clovis_st_op_launch(ops, ARRAY_SIZE(ops));

	/* We wait for each op. */
	for (i = 0; i < CREATE_MULTIPLE_N_OBJS; ++i) {
		/*
		 * Note: (Sining) M0_TIME_NEVER is not used here in order
		 * to test timeout handling (emulate a clovis application)
		 * in release_op(clovis_st_assert.c).
		 */
		rc = clovis_st_op_wait(ops[i], M0_BITS(M0_CLOVIS_OS_FAILED,
						       M0_CLOVIS_OS_STABLE),
				       m0_time_from_now(5,0));
		CLOVIS_ST_ASSERT_FATAL(rc == 0);
	}

	/* All correctly created. */
	for (i = 0; i < CREATE_MULTIPLE_N_OBJS; ++i) {
		CLOVIS_ST_ASSERT_FATAL(
			ops[i]->op_sm.sm_state == M0_CLOVIS_OS_STABLE);
	}

	/* Clean up. */
	for (i = 0; i < CREATE_MULTIPLE_N_OBJS; ++i) {
		clovis_st_op_fini(ops[i]);
		clovis_st_op_free(ops[i]);
		clovis_st_entity_fini(&objs[i]->ob_entity);
		mem_free(objs[i]);
	}

	mem_free(objs);
}

/**
 * Tries to delete an object that does not exist.
 */
static void obj_delete_non_existent(void)
{
	int                     rc;
	struct m0_clovis_op    *ops[1] = {NULL};
	struct m0_clovis_obj   *obj;
	struct m0_uint128       id;

	MEM_ALLOC_PTR(obj);

	/* initialise the id */
	clovis_oid_get(&id);

	clovis_st_obj_init(obj, &clovis_st_obj_container.co_realm, &id);

	/* Delete the entity. but it does not exist! I know! :'( */
	rc = clovis_st_entity_delete(&obj->ob_entity, &ops[0]);
	CLOVIS_ST_ASSERT_FATAL(rc == 0);
	CLOVIS_ST_ASSERT_FATAL(ops[0] != NULL);
	CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_rc == 0);

	clovis_st_op_launch(ops, ARRAY_SIZE(ops));
	rc = clovis_st_op_wait(ops[0],
			       M0_BITS(M0_CLOVIS_OS_FAILED,
				       M0_CLOVIS_OS_STABLE),
			       m0_time_from_now(5,0));
	CLOVIS_ST_ASSERT_FATAL(rc == 0);
	CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_state == M0_CLOVIS_OS_FAILED);
	CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_rc == -ENOENT);

	clovis_st_op_fini(ops[0]);
	clovis_st_op_free(ops[0]);
	clovis_st_entity_fini(&obj->ob_entity);
	mem_free(obj);
}

/**
 * Creates an object and then issues a new op. to delete it straightaway.
 */
static void obj_create_then_delete(void)
{
	int                     i;
	int                     rc;
	int                     rounds = 20;
	struct m0_clovis_op    *ops_c[1] = {NULL};
	struct m0_clovis_op    *ops_d[1] = {NULL};
	struct m0_clovis_obj   *obj;
	struct m0_uint128       id;

	MEM_ALLOC_PTR(obj);

	/* initialise the id */
	clovis_oid_get(&id);

	for (i = 0; i < rounds; i++) {
		clovis_st_obj_init(obj, &clovis_st_obj_container.co_realm, &id);

		/* Create the entity */
		rc = clovis_st_entity_create(&obj->ob_entity, &ops_c[0]);
		CLOVIS_ST_ASSERT_FATAL(rc == 0);
		CLOVIS_ST_ASSERT_FATAL(ops_c[0] != NULL);
		CLOVIS_ST_ASSERT_FATAL(ops_c[0]->op_sm.sm_rc == 0);

		clovis_st_op_launch(ops_c, ARRAY_SIZE(ops_c));
		rc = clovis_st_op_wait(ops_c[0], M0_BITS(M0_CLOVIS_OS_FAILED,
							 M0_CLOVIS_OS_STABLE),
				       m0_time_from_now(5,0));
		CLOVIS_ST_ASSERT_FATAL(rc == 0);
		CLOVIS_ST_ASSERT_FATAL(ops_c[0]->op_sm.sm_state == M0_CLOVIS_OS_STABLE);

		/* Delete the entity */
		clovis_st_entity_delete(&obj->ob_entity, &ops_d[0]);
		CLOVIS_ST_ASSERT_FATAL(ops_d[0] != NULL);
		CLOVIS_ST_ASSERT_FATAL(ops_d[0]->op_sm.sm_rc == 0);
		clovis_st_op_launch(ops_d, ARRAY_SIZE(ops_d));
		rc = clovis_st_op_wait(ops_d[0], M0_BITS(M0_CLOVIS_OS_FAILED,
							 M0_CLOVIS_OS_STABLE),
				       m0_time_from_now(5,0));
		CLOVIS_ST_ASSERT_FATAL(rc == 0);
		CLOVIS_ST_ASSERT_FATAL(ops_d[0]->op_sm.sm_state == M0_CLOVIS_OS_STABLE);

		clovis_st_op_fini(ops_d[0]);
		clovis_st_op_fini(ops_c[0]);
	}

	clovis_st_op_free(ops_d[0]);
	clovis_st_op_free(ops_c[0]);
	clovis_st_entity_fini(&obj->ob_entity);
	mem_free(obj);
}

/**
 * Arbitrarily creates and deletes objects within the same set of objects.
 * Launches different groups of operations each time.
 */
static void obj_delete_multiple(void)
{
	uint32_t                n_objs = 5;
	uint32_t                rounds = 20;
	uint32_t                max_ops = 4; /* max. ops in one launch() */
	struct m0_uint128      *ids = NULL;
	struct m0_clovis_obj   *objs = NULL;
	struct m0_clovis_op   **ops = NULL;
	bool                   *obj_used = NULL;
	bool                   *obj_exists = NULL;
	bool                   *already_chosen = NULL;
	uint32_t                i;
	uint32_t                j;
	uint32_t                idx;
	uint32_t                n_ops;
	int                     rc;

	/* Parameters are correct. */
	CLOVIS_ST_ASSERT_FATAL(n_objs >= max_ops);

	MEM_ALLOC_ARR(ids, n_objs);
	CLOVIS_ST_ASSERT_FATAL(ids != NULL);
	MEM_ALLOC_ARR(objs, n_objs);
	CLOVIS_ST_ASSERT_FATAL(objs != NULL);
	MEM_ALLOC_ARR(obj_used, n_objs);
	CLOVIS_ST_ASSERT_FATAL(obj_used != NULL);
	MEM_ALLOC_ARR(obj_exists, n_objs);
	CLOVIS_ST_ASSERT_FATAL(obj_exists != NULL);
	MEM_ALLOC_ARR(already_chosen, n_objs);
	CLOVIS_ST_ASSERT_FATAL(already_chosen != NULL);

	MEM_ALLOC_ARR(ops, max_ops);
	CLOVIS_ST_ASSERT_FATAL(ops != NULL);
	for (i = 0; i < max_ops; i++)
		ops[i] = NULL;

	/* Initialise the objects. Closed set. */
	for (i = 0; i < n_objs; ++i)
		clovis_oid_get(ids + i);

	memset(obj_exists, 0, n_objs*sizeof(obj_exists[0]));
	/* Repeat several rounds. */
	for (i = 0; i < rounds; ++i) {
		/* Each round, a different number of ops in the same launch. */
		n_ops = generate_random(max_ops) + 1;
		memset(already_chosen, 0, n_objs*sizeof(already_chosen[0]));

		/* Set the ops. */
		for (j = 0; j < n_ops; ++j) {
			/* Pick n_ops objects. */
			do {
				idx = generate_random(n_objs);
			} while(already_chosen[idx]);
			already_chosen[idx] = true;
			obj_used[idx] = true;

			clovis_st_obj_init(&objs[idx],
				&clovis_st_obj_container.co_realm, &ids[idx]);
			if (obj_exists[idx]) {
				rc = clovis_st_entity_delete(
					&objs[idx].ob_entity, &ops[j]);
				obj_exists[idx] = false;
				CLOVIS_ST_ASSERT_FATAL(ops[j] != NULL);
			} else {
				rc = clovis_st_entity_create(
					&objs[idx].ob_entity, &ops[j]);
				obj_exists[idx] = true;
				CLOVIS_ST_ASSERT_FATAL(ops[j] != NULL);
			}
			CLOVIS_ST_ASSERT_FATAL(rc == 0);
			CLOVIS_ST_ASSERT_FATAL(ops[j] != NULL);
			CLOVIS_ST_ASSERT_FATAL(ops[j]->op_sm.sm_rc == 0);
		}

		/* Launch and check. */
		clovis_st_op_launch(ops, n_ops);
		for (j = 0; j < n_ops; ++j) {
			rc = clovis_st_op_wait(ops[j],
					       M0_BITS(M0_CLOVIS_OS_FAILED,
						       M0_CLOVIS_OS_STABLE),
					       M0_TIME_NEVER);
					       //m0_time_from_now(5,0));
			CLOVIS_ST_ASSERT_FATAL(rc == 0);
			CLOVIS_ST_ASSERT_FATAL(ops[j]->op_sm.sm_state ==
					 M0_CLOVIS_OS_STABLE);
			clovis_st_op_fini(ops[j]);
		}
	}

	/* Clean up. */
	for (i = 0; i < max_ops; ++i) {
		if (ops[i] != NULL)
			clovis_st_op_free(ops[i]);
	}

	/* Clean up. */
	for (i = 0; i < n_objs; ++i) {
		if (obj_used[i] == true)
			clovis_st_entity_fini(&objs[i].ob_entity);
	}

	mem_free(ops);
	mem_free(already_chosen);
	mem_free(obj_exists);
	mem_free(objs);
	mem_free(ids);
}


/**
 * Launches a create object operation but does not call m0_clovis_op_wait().
 */
static void obj_no_wait(void)
{
	struct m0_clovis_op    *ops[] = {NULL};
	struct m0_clovis_obj   *obj;
	struct m0_uint128       id;
	int                     rc;

	MEM_ALLOC_PTR(obj);
	CLOVIS_ST_ASSERT_FATAL(obj != NULL);

	/* Initialise ids. */
	clovis_oid_get(&id);

	clovis_st_obj_init(obj, &clovis_st_obj_container.co_realm, &id);
	rc = clovis_st_entity_create(&obj->ob_entity, &ops[0]);
	CLOVIS_ST_ASSERT_FATAL(rc == 0);
	CLOVIS_ST_ASSERT_FATAL(ops[0] != NULL);
	CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_rc == 0);

	clovis_st_op_launch(ops, ARRAY_SIZE(ops));

	/* A call to m0_clovis_op_wait is not strictly required. */
#ifndef __KERNEL__
	sleep(5);
#else
	msleep(5000);
#endif

	CLOVIS_ST_ASSERT_FATAL(rc == 0);
	CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_state == M0_CLOVIS_OS_STABLE);

	clovis_st_op_fini(ops[0]);
	clovis_st_op_free(ops[0]);
	clovis_st_entity_fini(&obj->ob_entity);
	mem_free(obj);
}

/**
 * m0_clovis_op_wait() times out.
 */
static void obj_wait_no_launch(void)
{
	struct m0_clovis_op    *ops[] = {NULL};
	struct m0_clovis_obj   *obj;
	struct m0_uint128       id;
	int                     rc;

	MEM_ALLOC_PTR(obj);
	CLOVIS_ST_ASSERT_FATAL(obj != NULL);

	/* Initialise ids. */
	clovis_oid_get(&id);

	clovis_st_obj_init(obj, &clovis_st_obj_container.co_realm, &id);
	rc = clovis_st_entity_create(&obj->ob_entity, &ops[0]);
	CLOVIS_ST_ASSERT_FATAL(rc == 0);
	CLOVIS_ST_ASSERT_FATAL(ops[0] != NULL);
	CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_rc == 0);

	/* The operation is not launched so the state should not change. */

	rc = clovis_st_op_wait(ops[0], M0_BITS(M0_CLOVIS_OS_FAILED,
					       M0_CLOVIS_OS_STABLE),
				m0_time_from_now(3,0));
	CLOVIS_ST_ASSERT_FATAL(rc == -ETIMEDOUT);
	CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_state ==
			       M0_CLOVIS_OS_INITIALISED);

	clovis_st_op_fini(ops[0]);
	clovis_st_op_free(ops[0]);
	clovis_st_entity_fini(&obj->ob_entity);
	mem_free(obj);
}

/**
 * Launches a create object operation and waits() twice on it.
 */
static void obj_wait_twice(void)
{
	struct m0_clovis_op    *ops[] = {NULL};
	struct m0_clovis_obj   *obj;
	struct m0_uint128       id;
	int                     rc;

	MEM_ALLOC_PTR(obj);
	CLOVIS_ST_ASSERT_FATAL(obj != NULL);

	/* Initialise ids. */
	clovis_oid_get(&id);

	clovis_st_obj_init(obj, &clovis_st_obj_container.co_realm, &id);
	rc = clovis_st_entity_create(&obj->ob_entity, &ops[0]);
	CLOVIS_ST_ASSERT_FATAL(rc == 0);
	CLOVIS_ST_ASSERT_FATAL(ops[0] != NULL);
	CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_rc == 0);

	clovis_st_op_launch(ops, ARRAY_SIZE(ops));

	/* Calling op_wait() several times should have an innocuous effect*/
	rc = clovis_st_op_wait(ops[0], M0_BITS(M0_CLOVIS_OS_FAILED,
					       M0_CLOVIS_OS_STABLE),
				m0_time_from_now(5,0));

	CLOVIS_ST_ASSERT_FATAL(rc == 0);
	CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_state == M0_CLOVIS_OS_STABLE);

	rc = clovis_st_op_wait(ops[0], M0_BITS(M0_CLOVIS_OS_FAILED,
					       M0_CLOVIS_OS_STABLE),
				m0_time_from_now(5,0));
	CLOVIS_ST_ASSERT_FATAL(rc == 0);
	CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_state == M0_CLOVIS_OS_STABLE);

	clovis_st_op_fini(ops[0]);
	clovis_st_op_free(ops[0]);
	clovis_st_entity_fini(&obj->ob_entity);
	mem_free(obj);
}

static void mock_op_cb_stable(struct m0_clovis_op *op)
{
	int *val;

	val = (int *)op->op_datum;
	*val = 'S';
}

static void mock_op_cb_failed(struct m0_clovis_op *op)
{
	int *val;

	val = (int *)op->op_datum;
	*val = 'F';
}

/**
 * m0_clovis_op_setup() for entity op.
 */
static void obj_op_setup(void)
{
	int                      rc;
	int                      val = 0;
	struct m0_uint128        id;
	struct m0_clovis_obj    *obj;
	struct m0_clovis_op     *ops[] = {NULL};
	struct m0_clovis_op_ops  cbs;

	MEM_ALLOC_PTR(obj);
	CLOVIS_ST_ASSERT_FATAL(obj != NULL);

	/*
	 * Set callbacks for operations.
	 * oop_executed is not supported (see comments in clovis/clovis.h)
	 */
	cbs.oop_executed = NULL;
	cbs.oop_stable = mock_op_cb_stable;
	cbs.oop_failed = mock_op_cb_failed;

	/* Initilise an CREATE op. */
	clovis_oid_get(&id);
	clovis_st_obj_init(obj, &clovis_st_obj_container.co_realm, &id);
	rc = clovis_st_entity_create(&obj->ob_entity, &ops[0]);
	CLOVIS_ST_ASSERT_FATAL(rc == 0);
	CLOVIS_ST_ASSERT_FATAL(ops[0] != NULL);
	CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_rc == 0);

	/* Test callback functions for OS_STABLE*/
	ops[0]->op_datum = (void *)&val;
	m0_clovis_op_setup(ops[0], &cbs, 0);
	clovis_st_op_launch(ops, ARRAY_SIZE(ops));
	rc = clovis_st_op_wait(ops[0], M0_BITS(M0_CLOVIS_OS_FAILED,
					       M0_CLOVIS_OS_STABLE),
				m0_time_from_now(5,0));
	if (rc == 0) {
		if (ops[0]->op_sm.sm_state == M0_CLOVIS_OS_STABLE)
			CLOVIS_ST_ASSERT_FATAL(val == 'S')
		else if (ops[0]->op_sm.sm_state == M0_CLOVIS_OS_EXECUTED)
			CLOVIS_ST_ASSERT_FATAL(val == 'F')
	}

	clovis_st_op_fini(ops[0]);
	clovis_st_entity_fini(&obj->ob_entity);

	/* Test callback function for OS_FAILED*/
	memset(obj, 0, sizeof *obj);
	clovis_oid_get(&id);

	clovis_st_obj_init(obj, &clovis_st_obj_container.co_realm, &id);
	rc = clovis_st_entity_delete(&obj->ob_entity, &ops[0]);
	CLOVIS_ST_ASSERT_FATAL(rc == 0);

	ops[0]->op_datum = (void *)&val;
	m0_clovis_op_setup(ops[0], &cbs, 0);
	clovis_st_op_launch(ops, ARRAY_SIZE(ops));
	rc = clovis_st_op_wait(ops[0], M0_BITS(M0_CLOVIS_OS_FAILED,
					       M0_CLOVIS_OS_STABLE),
				m0_time_from_now(5,0));
	if (rc == 0) {
		CLOVIS_ST_ASSERT_FATAL(ops[0]->op_sm.sm_state
				       == M0_CLOVIS_OS_FAILED);
		CLOVIS_ST_ASSERT_FATAL(val == 'F');
	}

	clovis_st_op_fini(ops[0]);
	clovis_st_op_free(ops[0]);
	clovis_st_entity_fini(&obj->ob_entity);

	/* End of the test */
	mem_free(obj);
}

/**
 * Initialises the obj suite's environment.
 */
static int clovis_st_obj_suite_init(void)
{
	int rc = 0;

	/*
	 * Retrieve the uber realm. We don't need to open this,
	 * as realms are not actually implemented yet
	 */
	m0_clovis_container_init(&clovis_st_obj_container, NULL,
				 &M0_CLOVIS_UBER_REALM,
				 clovis_st_get_instance());
	rc = clovis_st_obj_container.co_realm.re_entity.en_sm.sm_rc;

	if (rc != 0)
		console_printf("Failed to open uber realm\n");

	test_id = M0_CLOVIS_ID_APP;
	test_id.u_lo += generate_random(0xffff);

	return rc;
}

/**
 * Finalises the obj suite's environment.
 */
static int clovis_st_obj_suite_fini(void)
{
	return 0;
}

struct clovis_st_suite st_suite_clovis_obj = {
	.ss_name = "clovis_obj_st",
	.ss_init = clovis_st_obj_suite_init,
	.ss_fini = clovis_st_obj_suite_fini,
	.ss_tests = {
		{ "obj_create_simple",
		  &obj_create_simple},
		{ "obj_create_double_same_id",
		  &obj_create_double_same_id},
		{ "obj_create_multiple_objects",
		  &obj_create_multiple_objects},
		{ "obj_delete_non_existent",
		  &obj_delete_non_existent},
		{ "obj_create_then_delete",
		  &obj_create_then_delete},
		{ "obj_delete_multiple",
		  &obj_delete_multiple},
		{ "obj_no_wait",
		  &obj_no_wait},
		{ "obj_wait_twice",
		  &obj_wait_twice},
		{ "obj_wait_no_launch",
		  &obj_wait_no_launch},
		{ "obj_op_setup",
		  &obj_op_setup},
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