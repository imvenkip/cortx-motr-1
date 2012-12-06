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
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 *		    Madhavrao Vemuri <madhav_vemuri@xyratex.com>
 * Original creation date: 06/27/2011
 */

#include "mero/init.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/getopts.h"
#include "lib/memory.h"
#include "lib/misc.h" /* M0_SET0 */
#include "lib/thread.h"
#include "lib/time.h"
#include "net/net.h"
#include "net/lnet/lnet.h"
#include "rpc/rpc.h"
#include "rpc/it/ping_fop.h"
#include "rpc/it/ping_fom.h"
#include "rpc/rpclib.h" /* m0_rpc_server_start */
#include "ut/rpc.h"     /* m0_rpc_client_init */
#include "ut/ut.h"
#include "fop/fop.h"    /* m0_fop_default_item_ops */
#include "reqh/reqh.h"  /* m0_reqh_rpc_mach_tl */
#include "rpc/it/ping_fop_ff.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#include "rpc/it/linux_kernel/rpc_ping.h"
#define printf printk
#else
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#endif

#ifndef __KERNEL__
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#include <arpa/inet.h>
#include <netdb.h>
#endif

#define TRANSPORT_NAME		"lnet"
#define SERVER_ENDPOINT         TRANSPORT_NAME ":" "0@lo:12345:34:1"

#define CLIENT_DB_FILE_NAME	"m0rpcping_client.db"

#define SERVER_DB_FILE_NAME	"m0rpcping_server.db"
#define SERVER_STOB_FILE_NAME	"m0rpcping_server.stob"
#define SERVER_LOG_FILE_NAME	"m0rpcping_server.log"

enum ep_type {
	EP_SERVER,
	EP_CLIENT,
};

enum {
	BUF_LEN		   = 128,
	STRING_LEN	   = 16,
	M0_LNET_PORTAL     = 34,
	MAX_RPCS_IN_FLIGHT = 32,
	CLIENT_COB_DOM_ID  = 13,
	CONNECT_TIMEOUT	   = 20,
};

#ifndef __KERNEL__
static bool server_mode = false;
#endif

static bool  verbose           = false;
static char *server_nid        = "0@lo";
static char *client_nid        = "0@lo";
static int   server_tmid       = 1;
static int   client_tmid       = 2;
static int   nr_client_threads = 1;
static int   nr_slots          = 1;
static int   nr_ping_bytes     = 8;
static int   nr_ping_item      = 1;
static int   tm_recv_queue_len = M0_NET_TM_RECV_QUEUE_DEF_LEN;
static int   max_rpc_msg_size  = M0_RPC_DEF_MAX_RPC_MSG_SIZE;

static char client_endpoint[M0_NET_LNET_XEP_ADDR_LEN];
static char server_endpoint[M0_NET_LNET_XEP_ADDR_LEN];

static struct m0_net_xprt *xprt = &m0_net_lnet_xprt;

#ifdef __KERNEL__
/* Module parameters */
module_param(verbose, bool, S_IRUGO);
MODULE_PARM_DESC(verbose, "enable verbose output to kernel log");

module_param(client_nid, charp, S_IRUGO);
MODULE_PARM_DESC(client_nid, "client network identifier");

module_param(server_nid, charp, S_IRUGO);
MODULE_PARM_DESC(server_nid, "server network identifier");

module_param(server_tmid, int, S_IRUGO);
MODULE_PARM_DESC(server_tmid, "remote transfer machine identifier");

module_param(client_tmid, int, S_IRUGO);
MODULE_PARM_DESC(client_tmid, "local transfer machine identifier");

module_param(nr_client_threads, int, S_IRUGO);
MODULE_PARM_DESC(nr_client_threads, "number of client threads");

module_param(nr_slots, int, S_IRUGO);
MODULE_PARM_DESC(nr_slots, "number of slots");

module_param(nr_ping_bytes, int, S_IRUGO);
MODULE_PARM_DESC(nr_ping_bytes, "number of ping fop bytes");

module_param(nr_ping_item, int, S_IRUGO);
MODULE_PARM_DESC(nr_ping_item, "number of ping fop items");

module_param(tm_recv_queue_len, int, S_IRUGO);
MODULE_PARM_DESC(tm_recv_queue_len, "minimum TM receive queue length");

module_param(max_rpc_msg_size, int, S_IRUGO);
MODULE_PARM_DESC(max_rpc_msg_size, "maximum RPC message size");
#endif

static int build_endpoint_addr(enum ep_type type, char *out_buf, size_t buf_size)
{
	char *ep_name;
	char *nid;
	int   tmid;

	switch (type) {
	case EP_SERVER:
		nid = server_nid;
		ep_name = "server";
		tmid = server_tmid;
		break;
	case EP_CLIENT:
		nid = client_nid;
		ep_name = "client";
		tmid = client_tmid;
		break;
	default:
		return -1;
	}

	if (buf_size > M0_NET_LNET_XEP_ADDR_LEN)
		return -1;
	else
		snprintf(out_buf, buf_size, "%s:%u:%u:%u", nid, M0_NET_LNET_PID,
			 M0_LNET_PORTAL, tmid);

	if (verbose)
		printf("%s endpoint: %s\n", ep_name, out_buf);

	return 0;
}

/* Get stats from rpc_machine and print them */
static void __print_stats(struct m0_rpc_machine *rpc_mach)
{
	struct m0_rpc_stats stats;
	printf("stats:\n");

	m0_rpc_machine_get_stats(rpc_mach, &stats, false);
	printf("\treceived_items: %llu\n",
	       (unsigned long long)stats.rs_nr_rcvd_items);
	printf("\tsent_items: %llu\n",
		(unsigned long long)stats.rs_nr_sent_items);
	printf("\tfailed_items: %llu\n",
		(unsigned long long)stats.rs_nr_failed_items);
	printf("\ttimedout_items: %llu\n",
		(unsigned long long)stats.rs_nr_timedout_items);
	printf("\tdropped_items: %llu\n",
		(unsigned long long)stats.rs_nr_dropped_items);

	printf("\treceived_packets: %llu\n",
	       (unsigned long long)stats.rs_nr_rcvd_packets);
	printf("\tsent_packets: %llu\n",
	       (unsigned long long)stats.rs_nr_sent_packets);
	printf("\tpackets_failed : %llu\n",
	       (unsigned long long)stats.rs_nr_failed_packets);

	printf("\tTotal_bytes_sent : %llu\n",
	       (unsigned long long)stats.rs_nr_sent_bytes);
	printf("\tTotal_bytes_rcvd : %llu\n",
	       (unsigned long long)stats.rs_nr_rcvd_bytes);
}

#ifndef __KERNEL__
/* Prints stats of all the rpc machines in the given request handler. */
static void print_stats(struct m0_reqh *reqh)
{
	struct m0_rpc_machine *rpcmach;

	M0_PRE(reqh != NULL);

	m0_rwlock_read_lock(&reqh->rh_rwlock);
	m0_tl_for(m0_reqh_rpc_mach, &reqh->rh_rpc_machines, rpcmach) {
		M0_ASSERT(m0_rpc_machine_bob_check(rpcmach));
		__print_stats(rpcmach);
	} m0_tl_endfor;
	m0_rwlock_read_unlock(&reqh->rh_rwlock);
}
#endif

/* Create a ping fop and post it to rpc layer */
static void send_ping_fop(struct m0_rpc_session *session)
{
	int                 i;
	int                 rc;
	struct m0_fop      *fop;
	struct m0_fop_ping *ping_fop;
	uint32_t            nr_arr_member;

	if (nr_ping_bytes % 8 == 0)
		nr_arr_member = nr_ping_bytes / 8;
	else
		nr_arr_member = nr_ping_bytes / 8 + 1;

	fop = m0_fop_alloc(&m0_fop_ping_fopt, NULL);
	M0_ASSERT(fop != NULL);

	ping_fop = m0_fop_data(fop);
	ping_fop->fp_arr.f_count = nr_arr_member;

	M0_ALLOC_ARR(ping_fop->fp_arr.f_data, nr_arr_member);
	M0_ASSERT(ping_fop->fp_arr.f_data != NULL);

	for (i = 0; i < nr_arr_member; i++)
		ping_fop->fp_arr.f_data[i] = i + 100;

	rc = m0_rpc_client_call(fop, session, NULL,
				m0_time_from_now(1, 0) /* deadline */,
				CONNECT_TIMEOUT);
	M0_ASSERT(rc == 0);
	M0_ASSERT(fop->f_item.ri_error == 0);
	M0_ASSERT(fop->f_item.ri_reply != 0);
	m0_fop_put(fop);
}

/*
 * An rpcping-specific implementation of client fini function, which is used
 * instead of m0_rpc_client_fini(). It's required in order to get a correct
 * statistics from rpc machine, which is possible only when all connections,
 * associated with rpc machine, are terminated, but rpc machine itself is not
 * finalized yet.
 */
static int client_fini(struct m0_rpc_client_ctx *cctx)
{
	int rc;

	rc = m0_rpc_session_destroy(&cctx->rcx_session, M0_TIME_NEVER);
	rc = m0_rpc_conn_destroy(&cctx->rcx_connection, M0_TIME_NEVER);
	m0_net_end_point_put(cctx->rcx_remote_ep);
	if (verbose)
		__print_stats(&cctx->rcx_rpc_machine);
	m0_rpc_machine_fini(&cctx->rcx_rpc_machine);
	m0_cob_domain_fini(cctx->rcx_cob_dom);
	m0_dbenv_fini(cctx->rcx_dbenv);
	m0_rpc_net_buffer_pool_cleanup(&cctx->rcx_buffer_pool);

	return rc;
}

static int run_client(void)
{
	int  rc;
	int  i;

	struct m0_thread *client_thread;

	/*
	 * Declare these variables as static, to avoid on-stack allocation
	 * of big structures. This is important for kernel-space, where stack
	 * size is very small.
	 */
	static struct m0_net_domain     client_net_dom;
	static struct m0_dbenv          client_dbenv;
	static struct m0_cob_domain     client_cob_dom;
	static struct m0_rpc_client_ctx cctx;

	cctx.rcx_net_dom               = &client_net_dom;
	cctx.rcx_local_addr            = client_endpoint;
	cctx.rcx_remote_addr           = server_endpoint;
	cctx.rcx_db_name               = CLIENT_DB_FILE_NAME;
	cctx.rcx_dbenv                 = &client_dbenv;
	cctx.rcx_cob_dom_id            = CLIENT_COB_DOM_ID;
	cctx.rcx_cob_dom               = &client_cob_dom;
	cctx.rcx_nr_slots              = nr_slots;
	cctx.rcx_timeout_s             = CONNECT_TIMEOUT;
	cctx.rcx_max_rpcs_in_flight    = MAX_RPCS_IN_FLIGHT;
	cctx.rcx_recv_queue_min_length = tm_recv_queue_len;
	cctx.rcx_max_rpc_msg_size      = max_rpc_msg_size,

	rc = build_endpoint_addr(EP_SERVER, server_endpoint,
				 sizeof(server_endpoint));
	if (rc != 0)
		return rc;

	rc = build_endpoint_addr(EP_CLIENT, client_endpoint,
				 sizeof(client_endpoint));
	if (rc != 0)
		return rc;

#ifndef __KERNEL__
	rc = m0_init();
	if (rc != 0)
		return rc;
#endif
	rc = m0_ping_fop_init();
	if (rc != 0)
		goto m0_fini;

	rc = m0_net_xprt_init(xprt);
	if (rc != 0)
		goto fop_fini;

	rc = m0_net_domain_init(&client_net_dom, xprt);
	if (rc != 0)
		goto xprt_fini;

	rc = m0_rpc_client_init(&cctx);
	if (rc != 0) {
#ifndef __KERNEL__
		printf("m0rpcping: client init failed \"%s\"\n", strerror(-rc));
#endif
		goto net_dom_fini;
	}
	M0_ALLOC_ARR(client_thread, nr_client_threads);

	for (i = 0; i < nr_client_threads; i++) {
		M0_SET0(&client_thread[i]);

		while (1) {
			m0_time_t t;

			rc = M0_THREAD_INIT(&client_thread[i],
					    struct m0_rpc_session*,
					    NULL, &send_ping_fop,
					    &cctx.rcx_session, "client_%d", i);
			if (rc == 0) {
				break;
			} else if (rc == EAGAIN) {
#ifndef __KERNEL__
				printf("Retrying thread init\n");
#endif
				m0_thread_fini(&client_thread[i]);
				m0_nanosleep(m0_time_set(&t, 1, 0), NULL);
			} else {
				M0_ASSERT("THREAD_INIT_FAILED" == NULL);
			}
		}
	}

	for (i = 0; i < nr_client_threads; i++) {
		m0_thread_join(&client_thread[i]);
	}
	/*
	 * NOTE: don't use m0_rpc_client_fini() here, see the comment above
	 * client_fini() for explanation.
	 */
	rc = client_fini(&cctx);

net_dom_fini:
	m0_net_domain_fini(&client_net_dom);
xprt_fini:
	m0_net_xprt_fini(xprt);
fop_fini:
	m0_ping_fop_fini();
m0_fini:
#ifndef __KERNEL__
	m0_fini();
#endif
	printf("fop_counter: %d\n", (int)m0_atomic64_get(&fop_counter));
	M0_ASSERT(m0_atomic64_get(&fop_counter) == 0);

	return rc;
}

#ifndef __KERNEL__
static void quit_dialog(void)
{
	char cli_buf[BUF_LEN];

	printf("\n########################################\n");
	printf("\n\nType \"quit\" or ^D to terminate\n\n");
	printf("\n########################################\n");
	while (fgets(cli_buf, BUF_LEN, stdin)) {
		if (strcmp(cli_buf, "quit\n") == 0)
			break;
		else {
			printf("\n########################################\n");
			printf("\n\nType \"quit\" or ^D to terminate\n\n");
			printf("\n########################################\n");
		}
	}
}

static int run_server(void)
{
	int	    rc;
	static char tm_len[STRING_LEN];
	static char rpc_size[STRING_LEN];

	char *server_argv[] = {
		"rpclib_ut", "-r", "-p", "-T", "AD", "-D", SERVER_DB_FILE_NAME,
		"-S", SERVER_STOB_FILE_NAME, "-e", server_endpoint,
		"-s", "ds1", "-s", "ds2", "-q", tm_len, "-m", rpc_size,
	};

	if (tm_recv_queue_len != 0)
		sprintf(tm_len, "%d" , tm_recv_queue_len);

	if (max_rpc_msg_size != 0)
		sprintf(rpc_size, "%d" , max_rpc_msg_size);

	M0_RPC_SERVER_CTX_DEFINE(sctx, &xprt, 1, server_argv,
				 ARRAY_SIZE(server_argv), m0_cs_default_stypes,
				 m0_cs_default_stypes_nr, SERVER_LOG_FILE_NAME);

	rc = m0_init();
	if (rc != 0)
		return rc;
	rc = m0_ut_init();
	if (rc != 0)
		goto m0_fini;

	rc = m0_ping_fop_init();
	if (rc != 0)
		goto m0_fini;

	/*
	 * Prepend transport name to the beginning of endpoint,
	 * as required by mero-setup.
	 */
	strcpy(server_endpoint, TRANSPORT_NAME ":");

	rc = build_endpoint_addr(
		EP_SERVER, server_endpoint + strlen(server_endpoint),
		sizeof(server_endpoint) - strlen(server_endpoint));
	if (rc != 0)
		return rc;

	rc = m0_rpc_server_start(&sctx);
	if (rc != 0)
		goto fop_fini;

	quit_dialog();

	if (verbose) {
		struct m0_reqh *reqh;

		reqh = m0_cs_reqh_get(&sctx.rsx_mero_ctx, "ds1");
		if (reqh != NULL) {
			printf("########### Server DS1 statS ###########\n");
			print_stats(reqh);
		}

		reqh = m0_cs_reqh_get(&sctx.rsx_mero_ctx, "ds2");
		if (reqh != NULL) {
			printf("\n");
			printf("########### Server DS2 statS ###########\n");
			print_stats(reqh);
		}
	}

	m0_rpc_server_stop(&sctx);
fop_fini:
	m0_ping_fop_fini();
	m0_ut_fini();
m0_fini:
	m0_fini();
	return rc;
}
#endif

#ifdef __KERNEL__
int m0_rpc_ping_init()
#else
/* Main function for rpc ping */
int main(int argc, char *argv[])
#endif
{
	int rc;

	m0_addb_choose_default_level(AEL_WARN);

#ifndef __KERNEL__
	rc = M0_GETOPTS("m0rpcping", argc, argv,
		M0_FLAGARG('s', "run server", &server_mode),
		M0_STRINGARG('C', "client nid",
			LAMBDA(void, (const char *str) { client_nid =
								(char*)str; })),
		M0_FORMATARG('p', "client tmid", "%i", &client_tmid),
		M0_STRINGARG('S', "server nid",
			LAMBDA(void, (const char *str) { server_nid =
								(char*)str; })),
		M0_FORMATARG('P', "server tmid", "%i", &server_tmid),
		M0_FORMATARG('b', "size in bytes", "%i", &nr_ping_bytes),
		M0_FORMATARG('t', "number of client threads", "%i",
						&nr_client_threads),
		M0_FORMATARG('l', "number of slots", "%i", &nr_slots),
		M0_FORMATARG('n', "number of ping items", "%i", &nr_ping_item),
		M0_FORMATARG('q', "minimum TM receive queue length", "%i",
						&tm_recv_queue_len),
		M0_FORMATARG('m', "maximum RPC msg size", "%i",
						&max_rpc_msg_size),
		M0_FLAGARG('v', "verbose", &verbose)
		);
	if (rc != 0)
		return rc;

	if (server_mode)
		rc = run_server();
	else
#endif
		rc = run_client();

	return rc;
}

M0_INTERNAL void m0_rpc_ping_fini(void)
{
}
