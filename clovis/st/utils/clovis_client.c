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
 * Original author: Rajanikant Chirmade <rajanikant.chirmade@seagate.com>
 * Original creation date: 29-Aug-2018
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>

#include "conf/obj.h"
#include "fid/fid.h"
#include "lib/memory.h"
#include "lib/trace.h"
#include "clovis/clovis.h"
#include "clovis/clovis_idx.h"
#include "clovis/st/utils/clovis_helper.h"

enum { CLOVIS_CMD_SIZE = 128 };

/* Clovis parameters */

static struct m0_clovis          *clovis_instance = NULL;
static struct m0_clovis_container clovis_container;
static struct m0_clovis_config    clovis_conf;
static struct m0_idx_dix_config   dix_conf;

extern struct m0_addb_ctx m0_clovis_addb_ctx;

static void help(void)
{
	m0_console_printf("Help:\n");
	m0_console_printf("touch    OBJ_ID\n");
	m0_console_printf("write   OBJ_ID SRC_FILE BLOCK_SIZE BLOCK_COUNT\n");
	m0_console_printf("read    OBJ_ID DEST_FILE BLOCK_SIZE BLOCK_COUNT\n");
	m0_console_printf("delete  OBJ_ID\n");
	m0_console_printf("help\n");
	m0_console_printf("quit\n");
}

#define CLOVIS_GET_ARG(arg, cmd, saveptr)       \
	arg = strtok_r(cmd, "\n: ", saveptr);   \
	if (arg == NULL) {                      \
		help();                         \
		continue;                       \
	}

int main(int argc, char **argv)
{
	struct m0_fid     fid;
	int               rc;
	char             *arg;
	char             *saveptr;
	char             *cmd;
	char             *fname = NULL;
	struct m0_uint128 id = M0_CLOVIS_ID_APP;
	int               block_size;
	int               block_count;

	/* Get input parameters */
	if (argc < 6) {
		m0_console_printf("Usage: c0client laddr ha_addr prof_opt"
		       " proc_fid read_verify_flag\n");
		return -1;
	}

	clovis_conf.cc_local_addr            = argv[1];
	clovis_conf.cc_ha_addr               = argv[2];
	clovis_conf.cc_profile               = argv[3];
	clovis_conf.cc_process_fid           = argv[4];
	clovis_conf.cc_is_read_verify        = strcmp(argv[5], "true") == 0;
	clovis_conf.cc_is_oostore            = true;
	clovis_conf.cc_tm_recv_queue_min_len = M0_NET_TM_RECV_QUEUE_DEF_LEN;
	clovis_conf.cc_max_rpc_msg_size      = M0_RPC_DEF_MAX_RPC_MSG_SIZE;
	clovis_conf.cc_idx_service_conf      = &dix_conf;
	dix_conf.kc_create_meta              = false;
	clovis_conf.cc_idx_service_id        = M0_CLOVIS_IDX_DIX;
	clovis_conf.cc_layout_id	     = 0;

	rc = clovis_init(&clovis_conf, &clovis_container, &clovis_instance);
	if (rc < 0) {
		m0_console_printf("clovis_init failed!\n");
		return rc;
	}

	memset(&fid, 0, sizeof fid);

	cmd = m0_alloc(CLOVIS_CMD_SIZE);
	if (cmd == NULL) {
		M0_ERR(-ENOMEM);
		goto cleanup;
	}

	do {
		m0_console_printf("c0clovis >>");
		arg = fgets(cmd, CLOVIS_CMD_SIZE, stdin);
		if (arg == NULL)
			continue;
		CLOVIS_GET_ARG(arg, cmd, &saveptr);
		if (strcmp(arg, "read") == 0) {
			CLOVIS_GET_ARG(arg, NULL, &saveptr);
			id.u_lo  = atoi(arg);
			CLOVIS_GET_ARG(arg, NULL, &saveptr);
			fname = strdup(arg);
			CLOVIS_GET_ARG(arg, NULL, &saveptr);
			block_size = atoi(arg);
			CLOVIS_GET_ARG(arg, NULL, &saveptr);
			block_count = atoi(arg);
			clovis_read(&clovis_container, id, fname,
				    block_size, block_count);
		} else if (strcmp(arg, "write") == 0) {
			CLOVIS_GET_ARG(arg, NULL, &saveptr);
			id.u_lo  = atoi(arg);
			CLOVIS_GET_ARG(arg, NULL, &saveptr);
			fname = strdup(arg);
			CLOVIS_GET_ARG(arg, NULL, &saveptr);
			block_size = atoi(arg);
			CLOVIS_GET_ARG(arg, NULL, &saveptr);
			block_count = atoi(arg);
			clovis_write(&clovis_container, fname, id,
				     block_size, block_count);
		} else if (strcmp(arg, "touch") == 0) {
			CLOVIS_GET_ARG(arg, NULL, &saveptr);
			id.u_lo  = atoi(arg);
			clovis_touch(&clovis_container, id);
		} else if (strcmp(arg, "delete") == 0) {
			CLOVIS_GET_ARG(arg, NULL, &saveptr);
			id.u_lo  = atoi(arg);
			clovis_unlink(&clovis_container, id);
		} else if (strcmp(arg, "help") == 0)
			help();
		else
			help();
	} while (arg == NULL || strcmp(arg, "quit") != 0);

	m0_free(cmd);
	if (fname != NULL)
		free(fname);

cleanup:
	/* Clean-up */
	clovis_fini(clovis_instance);

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
