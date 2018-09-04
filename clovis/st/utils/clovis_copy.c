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
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>

#include "conf/obj.h"
#include "fid/fid.h"
#include "lib/trace.h"
#include "clovis/clovis.h"
#include "clovis/clovis_idx.h"
#include "clovis/st/utils/clovis_helper.h"

static struct m0_clovis          *clovis_instance = NULL;
static struct m0_clovis_container clovis_container;
static struct m0_clovis_config    clovis_conf;
static struct m0_idx_dix_config   dix_conf;

extern struct m0_addb_ctx m0_clovis_addb_ctx;

int main(int argc, char **argv)
{
	int                rc;
	struct m0_uint128  id = M0_CLOVIS_ID_APP;
	char              *src_fname = NULL;
	uint32_t           block_size;
	uint32_t           block_count;

	/* Get input parameters */
	if (argc < 10) {
		printf("Usage: c0cp laddr ha_addr prof_opt proc_fid"
		       " read_verify_flag object_id src_file"
		       " block_size block_count\n");
		return -1;
	}

	clovis_conf.cc_local_addr            = argv[1];
	clovis_conf.cc_ha_addr               = argv[2];
	clovis_conf.cc_profile               = argv[3];
	clovis_conf.cc_process_fid           = argv[4];
	clovis_conf.cc_is_oostore            = true;
	clovis_conf.cc_tm_recv_queue_min_len = M0_NET_TM_RECV_QUEUE_DEF_LEN;
	clovis_conf.cc_max_rpc_msg_size      = M0_RPC_DEF_MAX_RPC_MSG_SIZE;
	clovis_conf.cc_idx_service_conf      = &dix_conf;
	dix_conf.kc_create_meta              = false;
	clovis_conf.cc_idx_service_id        = M0_CLOVIS_IDX_DIX;

	id.u_lo     = atoi(argv[6]);
	src_fname   = strdup(argv[7]);
	block_size  = atoi(argv[8]);
	block_count = atoi(argv[9]);

	if (strcmp(argv[5], "true") == 0)
		clovis_conf.cc_is_read_verify = true;
	else if (strcmp(argv[5], "false") == 0)
		clovis_conf.cc_is_read_verify = false;
	else {
		m0_console_printf("Invalid argument.\n");
		return -1;
	}

	/* Initilise mero and Clovis */
	rc = clovis_init(&clovis_conf, &clovis_container, &clovis_instance);
	if (rc < 0) {
		printf("clovis_init failed!\n");
		return rc;
	}

	clovis_write(&clovis_container, src_fname, id,
		     block_size, block_count);

	/* Clean-up */
	clovis_fini(clovis_instance);

	if (src_fname != NULL)
		free(src_fname);
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
