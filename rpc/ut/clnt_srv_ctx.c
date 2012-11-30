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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 09/28/2011
 */

#define CLIENT_ENDPOINT_ADDR    "0@lo:12345:34:*"
#define CLIENT_DB_NAME		"rpclib_ut_client.db"

#define SERVER_ENDPOINT_ADDR	"0@lo:12345:34:1"
#define SERVER_ENDPOINT		"lnet:" SERVER_ENDPOINT_ADDR
#define SERVER_DB_FILE_NAME	"rpc_ut_server.db"
#define SERVER_STOB_FILE_NAME	"rpc_ut_server.stob"
#define SERVER_LOG_FILE_NAME	"rpc_ut_server.log"

enum {
	CLIENT_COB_DOM_ID	= 16,
	SESSION_SLOTS		= 10,
	MAX_RPCS_IN_FLIGHT	= 1,
	CONNECT_TIMEOUT		= 5,
};

static struct c2_net_xprt    *xprt = &c2_net_lnet_xprt;
static struct c2_net_domain   client_net_dom = { };
static struct c2_dbenv        client_dbenv;
static struct c2_cob_domain   client_cob_dom;

static struct c2_rpc_client_ctx cctx = {
	.rcx_net_dom		   = &client_net_dom,
	.rcx_local_addr            = CLIENT_ENDPOINT_ADDR,
	.rcx_remote_addr           = SERVER_ENDPOINT_ADDR,
	.rcx_db_name		   = CLIENT_DB_NAME,
	.rcx_dbenv		   = &client_dbenv,
	.rcx_cob_dom_id		   = CLIENT_COB_DOM_ID,
	.rcx_cob_dom		   = &client_cob_dom,
	.rcx_nr_slots		   = SESSION_SLOTS,
	.rcx_timeout_s		   = CONNECT_TIMEOUT,
	.rcx_max_rpcs_in_flight	   = MAX_RPCS_IN_FLIGHT,
	.rcx_recv_queue_min_length = C2_NET_TM_RECV_QUEUE_DEF_LEN,
};

static char *server_argv[] = {
	"rpclib_ut", "-r", "-p", "-T", "AD", "-D", SERVER_DB_FILE_NAME,
	"-S", SERVER_STOB_FILE_NAME, "-e", SERVER_ENDPOINT,
	"-s", "ds1", "-s", "ds2"
};

static struct c2_rpc_server_ctx sctx = {
	.rsx_xprts            = &xprt,
	.rsx_xprts_nr         = 1,
	.rsx_argv             = server_argv,
	.rsx_argc             = ARRAY_SIZE(server_argv),
	.rsx_service_types    = c2_cs_default_stypes,
	.rsx_service_types_nr = 2,
	.rsx_log_file_name    = SERVER_LOG_FILE_NAME,
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
