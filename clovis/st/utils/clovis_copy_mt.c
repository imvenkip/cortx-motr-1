/* -*- C -*- */
/*
 * COPYRIGHT 2014 SEAGATE LLC
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
 * Original author:  Juan   Gonzalez <juan.gonzalez@seagate.com>
 *                   James  Morse    <james.s.morse@seagate.com>
 *                   Sining Wu       <sining.wu@seagate.com>
 * Revision:         Pratik Shinde   <pratik.shinde@seagate.com>
 * Original creation date: 30-Oct-2014
 */

/* This is a sample Clovis application. It will read data from
 * a file and write it to an object.
 * In General, steps are:
 * 1. Create a Clovis instance from the configuration data provided.
 * 2. Create an object.
 * 3. Read data from a file and fill it into Clovis buffers.
 * 4. Submit the write request.
 * 5. Wait for the write request to finish.
 * 6. Finalise the Clovis instance.
 */
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/time.h>
#include <pthread.h>

#include "clovis/clovis.h"
#include "clovis/clovis_internal.h"
#include "clovis/clovis_idx.h"

/* Currently Clovis can write at max 200 blocks in
 * a single request. This will change in future. */
#define CLOVIS_MAX_BLOCK_COUNT (200)

/* Clovis parameters */
/* local_addr is the Clovis endpoint on Mero cluster */
static char *clovis_local_addr;

/* End point of the HA service of Mero */
static char *clovis_ha_addr;

/* Mero profile to be used */
static char *clovis_prof;

/* Mero process fid*/
static char *clovis_proc_fid;

/* File name from which data is to be read */
static char  clovis_src[256];

/* Object id */
static int   clovis_oid;

/* Blocksize in which data is to be written
 * Clovis supports only 4K blocksize.
 * This will change in future.
 */
static int   clovis_block_size;

/* Number of blocks to be copied */
static int   clovis_block_count;

/* Index directory for KVS */
static char *clovis_index_dir = "/tmp/";

/* Clovis Instance */
static struct m0_clovis          *clovis_instance = NULL;

/* Clovis container */
static struct m0_clovis_container clovis_container;
static struct m0_clovis_realm     clovis_uber_realm;

/* Clovis Configuration */
static struct m0_clovis_config    clovis_conf;


/*
 * This function initialises Clovis and Mero.
 * Creates a Clovis instance and initializes the
 * realm to uber realm.
 */
static int init_clovis(void)
{
	int rc;

	/* Initialize Clovis configuration */
	clovis_conf.cc_is_oostore            = false;
	clovis_conf.cc_is_read_verify        = false;
	clovis_conf.cc_local_addr            = clovis_local_addr;
	clovis_conf.cc_ha_addr               = clovis_ha_addr;
	clovis_conf.cc_profile               = clovis_prof;
	clovis_conf.cc_process_fid           = clovis_proc_fid;
	clovis_conf.cc_tm_recv_queue_min_len = M0_NET_TM_RECV_QUEUE_DEF_LEN;
	clovis_conf.cc_max_rpc_msg_size      = M0_RPC_DEF_MAX_RPC_MSG_SIZE;

	/* Index service parameters */
	clovis_conf.cc_idx_service_id        = M0_CLOVIS_IDX_MOCK;
        clovis_conf.cc_idx_service_conf      = clovis_index_dir;
	clovis_conf.cc_layout_id	     = 0;

	/* Create Clovis instance */
	rc = m0_clovis_init(&clovis_instance, &clovis_conf, true);
	if (rc != 0) {
		printf("Failed to initilise Clovis\n");
		goto err_exit;
	}

	/* Container is where Entities (object) resides.
 	 * Currently, this feature is not implemented in Clovis.
 	 * We have only single realm: UBER REALM. In future with multiple realms
 	 * multiple applications can run in different containers. */
	m0_clovis_container_init(&clovis_container,
				 NULL, &M0_CLOVIS_UBER_REALM,
				 clovis_instance);
	rc = clovis_container.co_realm.re_entity.en_sm.sm_rc;

	if (rc != 0) {
		printf("Failed to open uber realm\n");
		goto err_exit;
	}

	clovis_uber_realm = clovis_container.co_realm;
	return 0;

err_exit:
	return rc;
}

static void fini_clovis(void)
{
	/* Finalize Clovis instance */
	m0_clovis_fini(clovis_instance, true);
}

static int create_object(struct m0_uint128 id)
{
	int                  rc;

	/* Clovis object */
	struct m0_clovis_obj obj;

	/* Clovis operation */
	struct m0_clovis_op *ops[1] = {NULL};

	memset(&obj, 0, sizeof(struct m0_clovis_obj));

 	/* Initialize obj structures
 	 * Note: This api doesnot create an object. It simply fills
 	 * obj structure with require data. */
	m0_clovis_obj_init(&obj, &clovis_uber_realm, &id,
			   m0_clovis_default_layout_id(clovis_instance));

	/* Create object-create request */
	m0_clovis_entity_create(&obj.ob_entity, &ops[0]);

       /* Launch the request. This is a asynchronous call.
 	* This will actually create an object */
	m0_clovis_op_launch(ops, ARRAY_SIZE(ops));

	/* Wait for the object creation to finish */
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
		rc = fread(data->ov_buf[i], clovis_block_size, 1, fp);
		if (rc != 1)
			break;

		if (feof(fp))
			break;
	}

	return i;
}

static int write_data_to_object(struct m0_uint128 id,
				struct m0_indexvec *ext,
				struct m0_bufvec *data,
				struct m0_bufvec *attr)
{
	int                  rc;
	struct m0_clovis_obj obj;
	struct m0_clovis_op *ops[1] = {NULL};

	memset(&obj, 0, sizeof(struct m0_clovis_obj));

	/* Set the object entity we want to write */
	m0_clovis_obj_init(&obj, &clovis_uber_realm, &id,
			   m0_clovis_default_layout_id(clovis_instance));

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

	/* fini and release */
	m0_clovis_op_fini(ops[0]);
	m0_clovis_op_free(ops[0]);
	m0_clovis_entity_fini(&obj.ob_entity);

	return rc;
}

/* This function will copy data from file to object.
 * General Algorithm is:
 * 1. Create an object.
 * 2. Read data from file and copy it into Clovis buffers.
 * 3. Create a write request and launch it.
 * 4. Wait for write to finish
 * 5. Submit next write request.
 */
static int copy()
{
	int                i;
	int                rc;
	int                block_count;
	uint64_t           last_index;

	/* Object id */
	struct m0_uint128  id;

	/* This is a extent vector of an object.
 	 * In this example, we set each extent
 	 * equal to blocksize. i.e. 4K */
	struct m0_indexvec ext;

	/* Clovis data buffer. Data to be written to
 	 * object will go here.	*/
	struct m0_bufvec   data;

	/* Block attributes. We don't use this in this demo. */
	struct m0_bufvec   attr;
	FILE              *fp;

	/* Open source file */
	fp = fopen(clovis_src, "r");
	if (fp == NULL) {
		printf("Cannot open file %s\n", clovis_src);
		return -1;
	}

	/* Create the object */
	id = M0_CLOVIS_ID_APP;
	id.u_lo = clovis_oid;
	rc = create_object(id);
	if (rc != 0) {
		fprintf(stderr, "Can't create object!\n");
		fclose(fp);
		return rc;
	}

	last_index = 0;

	/* Write data to the object.
 	 * Clovis writes at max 200 blocks in a single write request */
	while (clovis_block_count > 0) {
		block_count = (clovis_block_count > CLOVIS_MAX_BLOCK_COUNT)?
			      CLOVIS_MAX_BLOCK_COUNT:clovis_block_count;

		/* Allocate block_count * 4K data buffer. */
		rc = m0_bufvec_alloc(&data, block_count, clovis_block_size);
		if (rc != 0)
			return rc;

		/* Allocate bufvec for block Attribute
 		 * We don't use this in this demo */
		rc = m0_bufvec_alloc(&attr, block_count, 1);
		if(rc != 0)
			return rc;

		/* Allocate indexvec for block extends */
		rc = m0_indexvec_alloc(&ext, block_count);
		if (rc != 0)
			return rc;

		/*
		 * Prepare indexvec for write: <clovis_block_count> from the
		 * beginning of the object.
		 */
		for (i = 0; i < block_count; i++) {
			ext.iv_index[i] = last_index ;
			ext.iv_vec.v_count[i] = clovis_block_size;
			last_index += clovis_block_size;

			/* we don't want any attributes */
			attr.ov_vec.v_count[i] = 0;
		}

		/* Read data from source file. */
		rc = read_data_from_file(fp, &data);
		M0_ASSERT(rc == block_count);

		/* Copy data to the object*/
		rc = write_data_to_object(id, &ext, &data, &attr);
		if (rc != 0) {
			fprintf(stderr, "Writing to object failed!\n");
			return rc;
		}

		/* Free bufvec's and indexvec's */
		m0_indexvec_free(&ext);
		m0_bufvec_free(&data);
		m0_bufvec_free(&attr);

		clovis_block_count -= block_count;
	}

	fclose(fp);
	return 0;
}

#include "lib/thread.h"

static void *thread_copy(void *args)
{
	struct m0        *m0;
	struct m0_thread *mthread;

	printf("Entering copy thread\n");
	m0 = clovis_instance->m0c_mero;

	/* Make a 'mero' thread. */
	mthread = malloc(sizeof(struct m0_thread));
	if (mthread == NULL)
		return NULL;
	memset(mthread, 0, sizeof(struct m0_thread));
	m0_thread_adopt(mthread, m0);

	copy();

	/* Fini the thread. */
	m0_thread_shun();
	free(mthread);

	printf("Exit copy thread\n");
	return NULL;
}

int main(int argc, char **argv)
{
	int rc;
	pthread_t thr;

	/* Get input parameters */
	if (argc < 10) {
		printf("Usage: c0cp laddr ha_addr prof_opt proc_fid index_dir"
		       "object_id src_file block_size block_count\n");
		return -1;
	}
	clovis_local_addr = argv[1];;
	clovis_ha_addr = argv[2];
	clovis_prof = argv[3];
	clovis_proc_fid = argv[4];
	clovis_index_dir = argv[5];
	clovis_oid  = atoi(argv[6]);
	strcpy(clovis_src, argv[7]);
	clovis_block_size = atoi(argv[8]);
	clovis_block_count = atoi(argv[9]);

	/* Initilise mero and Clovis */
	rc = init_clovis();
	if (rc < 0) {
		printf("clovis_init failed!\n");
		return rc;
	}

	/* Read from the file and write to object */
	rc = pthread_create(&thr, NULL, thread_copy, NULL);
	if(rc !=0)
		fprintf(stderr, "Error %d while starting thread\n", rc);

	pthread_join(thr, NULL);

	/* Clean-up */
	fini_clovis();

	return 0;
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
