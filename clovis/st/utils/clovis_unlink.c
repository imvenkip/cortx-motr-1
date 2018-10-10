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
 * Original creation date: 30-Oct-2014
 */
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

#include "clovis/clovis.h"
#include "clovis/clovis_idx.h"

/* Clovis parameters */
static char *clovis_local_addr;
static char *clovis_ha_addr;
static char *clovis_prof;
static char *clovis_proc_fid;
static char *clovis_id;
static char *clovis_index_dir = "/tmp/";

static struct m0_clovis          *clovis_instance = NULL;
static struct m0_clovis_container clovis_container;
static struct m0_clovis_realm     clovis_uber_realm;
static struct m0_clovis_config    clovis_conf;

static int init_clovis(void)
{
	int rc;

	clovis_conf.cc_is_oostore            = false;
	clovis_conf.cc_is_read_verify        = false;
	clovis_conf.cc_local_addr            = clovis_local_addr;
	clovis_conf.cc_ha_addr               = clovis_ha_addr;
	clovis_conf.cc_profile               = clovis_prof;
	clovis_conf.cc_process_fid           = clovis_proc_fid;
	clovis_conf.cc_tm_recv_queue_min_len = M0_NET_TM_RECV_QUEUE_DEF_LEN;
	clovis_conf.cc_max_rpc_msg_size      = M0_RPC_DEF_MAX_RPC_MSG_SIZE;
	clovis_conf.cc_idx_service_id        = M0_CLOVIS_IDX_MOCK;
	clovis_conf.cc_idx_service_conf      = clovis_index_dir;

	/* Clovis instance */
	rc = m0_clovis_init(&clovis_instance, &clovis_conf, true);
	if (rc != 0) {
		printf("Failed to initilise Clovis\n");
		goto err_exit;
	}

	/* And finally, clovis root realm */
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
	m0_clovis_fini(clovis_instance, true);
}

static int unlink()
{
	int                     rc;
	struct m0_uint128       id;
	struct m0_clovis_op    *ops[1] = {NULL};
	struct m0_clovis_obj    obj;

	/* Initialise ids */
	id = M0_CLOVIS_ID_APP;
	id.u_lo = atoi(clovis_id);

	/* Create an entity */
	M0_SET0(&obj);
	m0_clovis_obj_init(&obj, &clovis_uber_realm, &id,
			   m0_clovis_default_layout_id(clovis_instance));
	m0_clovis_entity_delete(&obj.ob_entity, &ops[0]);

	m0_clovis_op_launch(ops, 1);

	rc = m0_clovis_op_wait(ops[0],
		M0_BITS(M0_CLOVIS_OS_FAILED, M0_CLOVIS_OS_STABLE),
		M0_TIME_NEVER);
	if (rc != 0)
		printf("c0unlink failed on waiting ops\n");

	/* fini and release */
	m0_clovis_op_fini(ops[0]);

	return 0;
}

int main(int argc, char **argv)
{
	int rc;

	/* Get input parameters */
	if (argc < 7) {
		printf("Usage: c0unlink laddr ha_addr prof_opt"
		       " proc_fid index_dir object_id\n");
		return -1;
	}

	clovis_local_addr = argv[1];
	clovis_ha_addr = argv[2];
	clovis_prof = argv[3];
	clovis_proc_fid = argv[4];
	clovis_index_dir = argv[5];
	clovis_id = argv[6];

	/* Initilise mero and Clovis */
	rc = init_clovis();
	if (rc < 0) {
		printf("clovis_init failed!\n");
		return rc;
	}

	/* Delete the object */
	unlink();

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
