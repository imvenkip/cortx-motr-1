/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 * Original creation date: 02/21/2011
 */

#include "bulkio_common.h"
#include "ut/cs_service.h"	/* ds1_service_type */
#ifndef __KERNEL__
#include <errno.h>
#endif

#define S_DBFILE		  "bulkio_st.db"
#define S_STOBFILE		  "bulkio_st_stob"

/* Global reference to bulkio_params structure. */
struct bulkio_params *bparm;

C2_TL_DESCR_DECLARE(rpcbulk, extern);

extern struct c2_reqh_service_type c2_ioservice_type;

#ifndef __KERNEL__
int bulkio_server_start(struct bulkio_params *bp, const char *saddr, int port)
{
	int			      i;
	int			      rc = 0;
	char			    **server_args;
	char			     xprt[IO_ADDR_LEN] = "bulk-sunrpc:";
	char			     sep[IO_ADDR_LEN];
	struct c2_rpc_server_ctx     *sctx;
	struct c2_reqh_service_type **stypes;

	C2_ASSERT(saddr != NULL);

	C2_ALLOC_ARR(server_args, IO_SERVER_ARGC);
	C2_ASSERT(server_args != NULL);
	if (server_args == NULL)
		return -ENOMEM;

	for (i = 0; i < IO_SERVER_ARGC; ++i) {
		C2_ALLOC_ARR(server_args[i], IO_ADDR_LEN);
		C2_ASSERT(server_args[i] != NULL);
		if (server_args[i] == NULL) {
			rc = -ENOMEM;
			break;
		}
	}

	if (rc != 0) {
		for (i = 0; i < IO_SERVER_ARGC; ++i)
			c2_free(server_args[i]);
		c2_free(server_args);
		return -ENOMEM;
	}

	/* Copy all server arguments to server_args list. */
	strcpy(server_args[0], "bulkio_st");
	strcpy(server_args[1], "-r");
	strcpy(server_args[2], "-T");
	strcpy(server_args[3], "AD");
	strcpy(server_args[4], "-D");
	strcpy(server_args[5], S_DBFILE);
	strcpy(server_args[6], "-S");
	strcpy(server_args[7], S_STOBFILE);
	strcpy(server_args[8], "-e");
	strcat(server_args[9], xprt);
	memset(sep, 0, IO_ADDR_LEN);
	bulkio_netep_form(saddr, port, IO_SERVER_SVC_ID, sep);
	strcat(server_args[9], sep);
	strcpy(server_args[10], "-s");
	strcpy(server_args[11], "ioservice");

	C2_ALLOC_ARR(stypes, IO_SERVER_SERVICE_NR);
	C2_ASSERT(stypes != NULL);
	stypes[0] = &ds1_service_type;

	C2_ALLOC_PTR(sctx);
	C2_ASSERT(sctx != NULL);

	sctx->rsx_xprts_nr = IO_XPRT_NR;
	sctx->rsx_argv = server_args;
	sctx->rsx_argc = IO_SERVER_ARGC;
	sctx->rsx_service_types = stypes;
	sctx->rsx_service_types_nr = IO_SERVER_SERVICE_NR;

	C2_ALLOC_ARR(bp->bp_slogfile, IO_STR_LEN);
	C2_ASSERT(bp->bp_slogfile != NULL);
	strcpy(bp->bp_slogfile, IO_SERVER_LOGFILE);
	sctx->rsx_log_file_name = bp->bp_slogfile;
	sctx->rsx_xprts = &bp->bp_xprt;

	rc = c2_rpc_server_start(sctx);
	C2_ASSERT(rc == 0);

	bp->bp_sctx = sctx;
	bparm = bp;
	return rc;
}

void bulkio_server_stop(struct c2_rpc_server_ctx *sctx)
{
	int i;

	C2_ASSERT(sctx != NULL);

	c2_rpc_server_stop(sctx);
	for (i = 0; i < IO_SERVER_ARGC; ++i)
		c2_free(sctx->rsx_argv[i]);

	c2_free(sctx->rsx_argv);
	c2_free(sctx->rsx_service_types);
	c2_free(sctx);
}
#endif
