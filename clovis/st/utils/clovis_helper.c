/* -*- C -*- */
/*
 * COPYRIGHT 2018 SEAGATE LLC
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF SEAGATE LLC,
 * ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF SEAGATE TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF SEAGATE LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF SEAGATE'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A SEAGATE REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author:  Rajanikant Chirmade <rajanikant.chirmade@seagate.com>
 * Original creation date: 25-Sept-2018
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>

#include "conf/obj.h"
#include "fid/fid.h"
#include "clovis/clovis.h"
#include "clovis/clovis_idx.h"

/** Max number of blocks in concurrent IO per thread. */
enum { CLOVIS_MAX_BLOCK_COUNT = 100 };

extern struct m0_addb_ctx m0_clovis_addb_ctx;

int clovis_init(struct m0_clovis_config    *config,
	        struct m0_clovis_container *clovis_container,
	        struct m0_clovis          **clovis_instance)
{
	int rc;

	rc = m0_clovis_init(clovis_instance, config, true);
	if (rc != 0)
		goto err_exit;

	m0_clovis_container_init(clovis_container,
				 NULL, &M0_CLOVIS_UBER_REALM,
				 *clovis_instance);
	rc = clovis_container->co_realm.re_entity.en_sm.sm_rc;

err_exit:
	return rc;
}

void clovis_fini(struct m0_clovis *clovis_instance)
{
	m0_clovis_fini(clovis_instance, true);
}

static void open_entity(struct m0_clovis_entity *entity)
{
	struct m0_clovis_op *ops[1] = {NULL};

	m0_clovis_entity_open(entity, &ops[0]);
	m0_clovis_op_launch(ops, 1);
	m0_clovis_op_wait(ops[0], M0_BITS(M0_CLOVIS_OS_FAILED,
					  M0_CLOVIS_OS_STABLE),
			  M0_TIME_NEVER);
	m0_clovis_op_fini(ops[0]);
	m0_clovis_op_free(ops[0]);
	ops[0] = NULL;
}

int create_object(struct m0_clovis_container *clovis_container,
		  struct m0_uint128 id)
{
	int                  rc = 0;
	struct m0_clovis_obj obj;
	struct m0_clovis_op *ops[1] = {NULL};
	struct m0_clovis    *clovis_instance;

	memset(&obj, 0, sizeof(struct m0_clovis_obj));

	clovis_instance = clovis_container->co_realm.re_instance;
	m0_clovis_obj_init(&obj, &clovis_container->co_realm, &id,
			   m0_clovis_layout_id(clovis_instance));

	open_entity(&obj.ob_entity);

	m0_clovis_entity_create(NULL, &obj.ob_entity, &ops[0]);

	m0_clovis_op_launch(ops, ARRAY_SIZE(ops));

	rc = m0_clovis_op_wait(
		ops[0], M0_BITS(M0_CLOVIS_OS_FAILED, M0_CLOVIS_OS_STABLE),
		m0_time_from_now(3,0));

	m0_clovis_op_fini(ops[0]);
	m0_clovis_op_free(ops[0]);
	m0_clovis_entity_fini(&obj.ob_entity);

	return rc;
}

static int read_data_from_file(FILE *fp, struct m0_bufvec *data)
{
	int i;
	int rc;
	int nr_blocks;

	nr_blocks = data->ov_vec.v_nr;
	for (i = 0; i < nr_blocks; i++) {
		rc = fread(data->ov_buf[i], data->ov_vec.v_count[i], 1, fp);
		if (rc != 1)
			break;

		if (feof(fp))
			break;
	}

	return i;
}

static int write_data_to_object(struct m0_clovis_container *clovis_container,
				struct m0_uint128 id,
				struct m0_indexvec *ext,
				struct m0_bufvec *data,
				struct m0_bufvec *attr)
{
	int                  rc;
	int                  op_rc;
	int                  nr_tries = 10;
	struct m0_clovis_obj obj;
	struct m0_clovis_op *ops[1] = {NULL};
	struct m0_clovis    *clovis_instance;

again:
	memset(&obj, 0, sizeof(struct m0_clovis_obj));

	clovis_instance = clovis_container->co_realm.re_instance;
	/* Set the object entity we want to write */
	m0_clovis_obj_init(&obj, &clovis_container->co_realm, &id,
			   m0_clovis_layout_id(clovis_instance));

	open_entity(&obj.ob_entity);

	/* Create the write request */
	m0_clovis_obj_op(&obj, M0_CLOVIS_OC_WRITE,
			 ext, data, attr, 0, &ops[0]);

	/* Launch the write request*/
	m0_clovis_op_launch(ops, 1);

	/* wait */
	rc = m0_clovis_op_wait(ops[0],
			M0_BITS(M0_CLOVIS_OS_FAILED,
				M0_CLOVIS_OS_STABLE),
			M0_TIME_NEVER);
	op_rc = ops[0]->op_sm.sm_rc;

	/* fini and release */
	m0_clovis_op_fini(ops[0]);
	m0_clovis_op_free(ops[0]);
	m0_clovis_entity_fini(&obj.ob_entity);

	if (op_rc == -EINVAL && nr_tries != 0) {
		nr_tries--;
		ops[0] = NULL;
		sleep(5);
		goto again;
	}

	return rc;
}

int clovis_touch(struct m0_clovis_container *clovis_container,
		 struct m0_uint128 id)
{
	return create_object(clovis_container, id);
}

int clovis_write(struct m0_clovis_container *clovis_container,
		char *src, struct m0_uint128 id,
		uint32_t block_size, uint32_t block_count)
{
	int                i;
	int                rc;
	uint64_t           last_index;
	struct m0_indexvec ext;
	struct m0_bufvec   data;
	struct m0_bufvec   attr;
	uint32_t           bcount;
	FILE              *fp;

	/* Open source file */
	fp = fopen(src, "r");
	if (fp == NULL)
		return -1;

	/* Create the object */
	rc = create_object(clovis_container, id);
	if (rc != 0) {
		fclose(fp);
		return rc;
	}

	last_index = 0;
	while (block_count > 0) {
		bcount = (block_count > CLOVIS_MAX_BLOCK_COUNT)?
			    CLOVIS_MAX_BLOCK_COUNT:block_count;

		/* Allocate block_count * block_size data buffer. */
		rc = m0_bufvec_alloc(&data, bcount, block_size);
		if (rc != 0)
			return rc;

		/* Allocate bufvec and indexvec for write. */
		rc = m0_bufvec_alloc(&attr, bcount, 1);
		if (rc != 0)
			return rc;

		rc = m0_indexvec_alloc(&ext, bcount);
		if (rc != 0)
			return rc;

		/*
		 * Prepare indexvec for write: <clovis_block_count> from the
		 * beginning of the object.
		 */
		for (i = 0; i < bcount; i++) {
			ext.iv_index[i] = last_index;
			ext.iv_vec.v_count[i] = block_size;
			last_index += block_size;

			/* we don't want any attributes */
			attr.ov_vec.v_count[i] = 0;
		}

		/* Read data from source file. */
		rc = read_data_from_file(fp, &data);
		M0_ASSERT(rc == bcount);

		/* Copy data to the object*/
		rc = write_data_to_object(clovis_container, id,
					  &ext, &data, &attr);
		if (rc != 0) {
			fprintf(stderr, "Writing to object failed!\n");
			return rc;
		}

		/* Free bufvec's and indexvec's */
		m0_indexvec_free(&ext);
		m0_bufvec_free(&data);
		m0_bufvec_free(&attr);

		block_count -= bcount;
	}

	fclose(fp);
	return rc;
}

int clovis_read(struct m0_clovis_container *clovis_container,
		struct m0_uint128 id, char *dest,
		uint32_t block_size, uint32_t block_count)
{
	int                  i;
	int                  j;
	int                  rc;
	struct m0_clovis_op *ops[1] = {NULL};
	struct m0_clovis_obj obj;
	uint64_t             last_index;
	struct m0_indexvec   ext;
	struct m0_bufvec     data;
	struct m0_bufvec     attr;
	FILE                *fp = NULL;
	struct m0_clovis    *clovis_instance;

	rc = m0_indexvec_alloc(&ext, block_count);
	if (rc != 0)
		return rc;

	/*
	 * this allocates <block_count> * <block_size>  buffers for data,
	 * and initialises the bufvec for us.
	 */
	rc = m0_bufvec_alloc(&data, block_count, block_size);
	if (rc != 0) {
		m0_indexvec_free(&ext);
		return rc;
	}

	rc = m0_bufvec_alloc(&attr, block_count, 1);
	if (rc != 0) {
		m0_indexvec_free(&ext);
		m0_bufvec_free(&data);
		return rc;
	}

	last_index = 0;
	for (i = 0; i < block_count; i++) {
		ext.iv_index[i] = last_index ;
		ext.iv_vec.v_count[i] = block_size;
		last_index += block_size;

		/* we don't want any attributes */
		attr.ov_vec.v_count[i] = 0;

	}

	clovis_instance = clovis_container->co_realm.re_instance;
	/* Read the requisite number of blocks from the entity */
	M0_SET0(&obj);
	m0_clovis_obj_init(&obj, &clovis_container->co_realm, &id,
			   m0_clovis_layout_id(clovis_instance));

	open_entity(&obj.ob_entity);

	/* Create the read request */
	m0_clovis_obj_op(&obj, M0_CLOVIS_OC_READ, &ext,
			 &data, &attr, 0, &ops[0]);
	M0_ASSERT(rc == 0);
	M0_ASSERT(ops[0] != NULL);
	M0_ASSERT(ops[0]->op_sm.sm_rc == 0);

	m0_clovis_op_launch(ops, 1);

	/* wait */
	rc = m0_clovis_op_wait(ops[0],
			    M0_BITS(M0_CLOVIS_OS_FAILED,
				    M0_CLOVIS_OS_STABLE),
		     M0_TIME_NEVER);
	M0_ASSERT(rc == 0);
	M0_ASSERT(ops[0]->op_sm.sm_state == M0_CLOVIS_OS_STABLE);
	M0_ASSERT(ops[0]->op_sm.sm_rc == 0);

	if (dest != NULL) {
		fp = fopen(dest, "w");
		if (fp == NULL) {
			rc = -1;
			goto cleanup;
		}
	}

	/* putchar the output */
	for (i = 0; i < block_count; i++) {
		for (j = 0; j < data.ov_vec.v_count[i]; j++) {
			if (dest != NULL)
				putc(((char *)data.ov_buf[i])[j], fp);
			else
				putchar(((char *)data.ov_buf[i])[j]);
		}
	}

	if (dest != NULL)
		fclose(fp);

cleanup:
	/* fini and release */
	m0_clovis_op_fini(ops[0]);
	m0_clovis_op_free(ops[0]);
	m0_clovis_entity_fini(&obj.ob_entity);

	m0_indexvec_free(&ext);
	m0_bufvec_free(&data);
	m0_bufvec_free(&attr);
	return rc;
}

int clovis_unlink(struct m0_clovis_container *clovis_container,
		  struct m0_uint128 id)
{
	int                   rc;
	struct m0_clovis_op  *ops[1] = {NULL};
	struct m0_clovis_obj  obj;
	struct m0_clovis     *clovis_instance;

	clovis_instance = clovis_container->co_realm.re_instance;
	/* Create an entity */
	M0_SET0(&obj);
	m0_clovis_obj_init(&obj, &clovis_container->co_realm, &id,
			   m0_clovis_layout_id(clovis_instance));
	open_entity(&obj.ob_entity);
	m0_clovis_entity_delete(&obj.ob_entity, &ops[0]);

	m0_clovis_op_launch(ops, 1);

	rc = m0_clovis_op_wait(ops[0],
		M0_BITS(M0_CLOVIS_OS_FAILED, M0_CLOVIS_OS_STABLE),
		M0_TIME_NEVER);

	/* fini and release */
	m0_clovis_op_fini(ops[0]);

	return rc;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
