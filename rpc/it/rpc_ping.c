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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "colibri/init.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/getopts.h"
#include "lib/memory.h"
#include "lib/misc.h" /* C2_SET0 */
#include "lib/thread.h"
#include "lib/processor.h"
#include "lib/trace.h"
#include "lib/time.h"
#include "net/net.h"
#include "net/lnet/lnet.h"
#include "rpc/rpc2.h"
#include "rpc/it/ping_fop.h"
#include "rpc/it/ping_fom.h"
#include "rpc/rpclib.h" /* c2_rpc_server_start */
#include "ut/rpc.h"     /* c2_rpc_client_init */
#include "fop/fop.h"    /* c2_fop_default_item_ops */
#include "reqh/reqh.h"  /* c2_reqh_rpc_mach_tl */
#include "xcode/xcode.h"
#include "rpc/it/ping_fop_xc.h"

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

#define CLIENT_DB_FILE_NAME	"rpcping_client.db"

#define SERVER_DB_FILE_NAME	"rpcping_server.db"
#define SERVER_STOB_FILE_NAME	"rpcping_server.stob"
#define SERVER_LOG_FILE_NAME	"rpcping_server.log"

enum ep_type {
	EP_SERVER,
	EP_CLIENT,
};

enum {
	BUF_LEN		   = 128,
	STRING_LEN	   = 16,
	C2_LNET_PORTAL     = 34,
	MAX_RPCS_IN_FLIGHT = 32,
	CLIENT_COB_DOM_ID  = 13,
	CONNECT_TIMEOUT	   = 60,
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
static int   tm_recv_queue_len = C2_NET_TM_RECV_QUEUE_DEF_LEN;
static int   max_rpc_msg_size  = C2_RPC_DEF_MAX_RPC_MSG_SIZE;

static char client_endpoint[C2_NET_LNET_XEP_ADDR_LEN];
static char server_endpoint[C2_NET_LNET_XEP_ADDR_LEN];

static struct c2_net_xprt *xprt = &c2_net_lnet_xprt;

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
MODULE_PARM_DESC(tm_recv_queue_len, "maximum RPC message size");
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

	if (buf_size > C2_NET_LNET_XEP_ADDR_LEN)
		return -1;
	else
		snprintf(out_buf, buf_size, "%s:%u:%u:%u", nid, C2_NET_LNET_PID,
			 C2_LNET_PORTAL, tmid);

	if (verbose)
		printf("%s endpoint: %s\n", ep_name, out_buf);

	return 0;
}

static void print_rpc_stats(struct c2_rpc_stats *stats)
{
	uint64_t nsec;
#ifdef __KERNEL__
	uint64_t sec = 0;
	uint64_t usec = 0;
	uint64_t thruput;
	uint64_t packing_density;
#else
	double   sec = 0;
	double   msec = 0;
	double   thruput;
	double   packing_density;
#endif

	printf("                rpcs:   %llu\n",
			(unsigned long long) stats->rs_rpcs_nr);
	printf("                items:  %llu\n",
			(unsigned long long) stats->rs_items_nr);
	printf("                bytes:  %llu\n",
			(unsigned long long) stats->rs_bytes_nr);
#ifndef __KERNEL__
	packing_density = (double) stats->rs_items_nr /
			  (double) stats->rs_rpcs_nr;
	printf("                packing_density: %lf\n", packing_density);
#else
	packing_density = (uint64_t) stats->rs_items_nr / stats->rs_rpcs_nr;
	printf("                packing_density: %llu\n", packing_density);
#endif
	sec = 0;
	sec = c2_time_seconds(stats->rs_min_lat);
	nsec = c2_time_nanoseconds(stats->rs_min_lat);
#ifdef __KERNEL__
	usec = (uint64_t) nsec / 1000;
	usec += (uint64_t) (sec * 1000000);
	printf("                min_latency: %llu # usec\n", usec);
	if (usec != 0) {
		thruput = (uint64_t)stats->rs_bytes_nr/usec;
		printf("                max_throughput: %llu # MB/s\n",
					thruput);
	}
#else
	sec += (double) nsec/1000000000;
	msec = (double) sec * 1000;
	printf("                min_latency:\t %lf # msec\n", msec);

	thruput = (double)stats->rs_bytes_nr/(sec*1000000);
	printf("                max_throughput:\t %lf # MB/s\n", thruput);
#endif

	sec = 0;
	sec = c2_time_seconds(stats->rs_max_lat);
	nsec = c2_time_nanoseconds(stats->rs_max_lat);
#ifdef __KERNEL__
	usec = (uint64_t) nsec / 1000;
	usec += (uint64_t) (sec * 1000000);
	printf("                max_latency: %llu # usec\n", usec);
	if (usec != 0) {
		thruput = (uint64_t)stats->rs_bytes_nr/usec;
		printf("                min_throughput: %llu # MB/s\n",
					thruput);
	}
#else
	sec += (double) nsec/1000000000;
	msec = (double) sec * 1000;
	printf("                max_latency:\t %lf # msec\n", msec);

	thruput = (double)stats->rs_bytes_nr/(sec*1000000);
	printf("                min_throughput:\t %lf # MB/s\n", thruput);
#endif
}

/* Get stats from rpc_machine and print them */
static void __print_stats(struct c2_rpc_machine *rpc_mach)
{
	printf("stats:\n");

	printf("        in:\n");
	print_rpc_stats(&rpc_mach->rm_rpc_stats[C2_RPC_PATH_INCOMING]);

	printf("        out:\n");
	print_rpc_stats(&rpc_mach->rm_rpc_stats[C2_RPC_PATH_OUTGOING]);
}

#ifndef __KERNEL__
/* Prints stats of all the rpc machines in the given request handler. */
static void print_stats(struct c2_reqh *reqh)
{
	struct c2_rpc_machine *rpcmach;

	C2_PRE(reqh != NULL);

	c2_rwlock_read_lock(&reqh->rh_rwlock);
	c2_tl_for(c2_reqh_rpc_mach, &reqh->rh_rpc_machines, rpcmach) {
		C2_ASSERT(c2_rpc_machine_bob_check(rpcmach));
		__print_stats(rpcmach);
	} c2_tl_endfor;
	c2_rwlock_read_unlock(&reqh->rh_rwlock);
}
#endif

/* Create a ping fop and post it to rpc layer */
static void send_ping_fop(struct c2_rpc_session *session)
{
	int                rc;
	int                i;
	struct c2_fop      *fop;
	struct c2_fop_ping *ping_fop;
	uint32_t           nr_arr_member;

	if (nr_ping_bytes % 8 == 0)
		nr_arr_member = nr_ping_bytes / 8;
	else
		nr_arr_member = nr_ping_bytes / 8 + 1;

	fop = c2_fop_alloc(&c2_fop_ping_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	ping_fop = c2_fop_data(fop);
	ping_fop->fp_arr.f_count = nr_arr_member;

	C2_ALLOC_ARR(ping_fop->fp_arr.f_data, nr_arr_member);
	if (ping_fop->fp_arr.f_data == NULL) {
		rc = -ENOMEM;
		goto free_fop;
	}

	for (i = 0; i < nr_arr_member; i++) {
		ping_fop->fp_arr.f_data[i] = i+100;
	}

	rc = c2_rpc_client_call(fop, session, &c2_fop_default_item_ops,
				CONNECT_TIMEOUT);
	C2_ASSERT(rc == 0);
	C2_ASSERT(fop->f_item.ri_error == 0);
	C2_ASSERT(fop->f_item.ri_reply != 0);

	c2_free(ping_fop->fp_arr.f_data);
free_fop:
	/* FIXME: freeing fop here will lead to endless loop in
	 * nr_active_items_count(), which is called from
	 * c2_rpc_session_terminate() */
	/*c2_fop_free(fop);*/
out:
	return;
}

/*
 * An rpcping-specific implementation of client fini function, which is used
 * instead of c2_rpc_client_fini(). It's required in order to get a correct
 * statistics from rpc machine, which is possible only when all connections,
 * associated with rpc machine, are terminated, but rpc machine itself is not
 * finalized yet.
 */
static int client_fini(struct c2_rpc_client_ctx *cctx)
{
	int rc;

	rc = c2_rpc_session_destroy(&cctx->rcx_session, cctx->rcx_timeout_s);
	if (rc != 0)
		return rc;

	rc = c2_rpc_conn_destroy(&cctx->rcx_connection, cctx->rcx_timeout_s);
	if (rc != 0)
		return rc;

	c2_net_end_point_put(cctx->rcx_remote_ep);

	if (verbose)
		__print_stats(&cctx->rcx_rpc_machine);

	c2_rpc_machine_fini(&cctx->rcx_rpc_machine);
	c2_cob_domain_fini(cctx->rcx_cob_dom);
	c2_dbenv_fini(cctx->rcx_dbenv);

	c2_rpc_net_buffer_pool_cleanup(&cctx->rcx_buffer_pool);

	return rc;
}

static int run_client(void)
{
	int  rc;
	int  i;

	struct c2_thread *client_thread;

	/*
	 * Declare these variables as static, to avoid on-stack allocation
	 * of big structures. This is important for kernel-space, where stack
	 * size is very small.
	 */
	static struct c2_net_domain     client_net_dom;
	static struct c2_dbenv          client_dbenv;
	static struct c2_cob_domain     client_cob_dom;
	static struct c2_rpc_client_ctx cctx;

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
	rc = c2_init();
	if (rc != 0)
		return rc;

	rc = c2_processors_init();
	if (rc != 0)
		goto c2_fini;
#endif
	rc = c2_ping_fop_init();
	if (rc != 0)
		goto proc_fini;

	rc = c2_net_xprt_init(xprt);
	if (rc != 0)
		goto fop_fini;

	rc = c2_net_domain_init(&client_net_dom, xprt);
	if (rc != 0)
		goto xprt_fini;

	rc = c2_rpc_client_init(&cctx);
	if (rc != 0)
		goto net_dom_fini;

	C2_ALLOC_ARR(client_thread, nr_client_threads);

	for (i = 0; i < nr_client_threads; i++) {
		C2_SET0(&client_thread[i]);

		while (1) {
			c2_time_t t;

			rc = C2_THREAD_INIT(&client_thread[i],
					    struct c2_rpc_session*,
					    NULL, &send_ping_fop,
					    &cctx.rcx_session, "client_%d", i);
			if (rc == 0) {
				break;
			} else if (rc == EAGAIN) {
#ifndef __KERNEL__
				printf("Retrying thread init\n");
#endif
				c2_thread_fini(&client_thread[i]);
				c2_nanosleep(c2_time_set(&t, 1, 0), NULL);
			} else {
				C2_ASSERT("THREAD_INIT_FAILED" == NULL);
			}
		}
	}

	for (i = 0; i < nr_client_threads; i++) {
		c2_thread_join(&client_thread[i]);
	}

	/*
	 * NOTE: don't use c2_rpc_client_fini() here, see the comment above
	 * client_fini() for explanation.
	 */
	rc = client_fini(&cctx);

net_dom_fini:
	c2_net_domain_fini(&client_net_dom);
xprt_fini:
	c2_net_xprt_fini(xprt);
fop_fini:
	c2_ping_fop_fini();
proc_fini:
#ifndef __KERNEL__
	c2_processors_fini();
c2_fini:
	c2_fini();
#endif
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
		"rpclib_ut", "-r", "-T", "AD", "-D", SERVER_DB_FILE_NAME,
		"-S", SERVER_STOB_FILE_NAME, "-e", server_endpoint,
		"-s", "ds1", "-s", "ds2", "-q", tm_len, "-m", rpc_size,
	};

	if (tm_recv_queue_len != 0)
		sprintf(tm_len, "%d" , tm_recv_queue_len);

	if (max_rpc_msg_size != 0)
		sprintf(rpc_size, "%d" , max_rpc_msg_size);

	C2_RPC_SERVER_CTX_DECLARE(sctx, &xprt, 1, server_argv,
				  ARRAY_SIZE(server_argv), SERVER_LOG_FILE_NAME);

	rc = c2_init();
	if (rc != 0)
		return rc;

	rc = c2_ping_fop_init();
	if (rc != 0)
		goto c2_fini;

	/*
	 * Prepend transport name to the beginning of endpoint,
	 * as required by colibri-setup.
	 */
	strcpy(server_endpoint, TRANSPORT_NAME ":");

	rc = build_endpoint_addr(
		EP_SERVER, server_endpoint + strlen(server_endpoint),
		sizeof(server_endpoint) - strlen(server_endpoint));
	if (rc != 0)
		return rc;

	rc = c2_rpc_server_start(&sctx);
	if (rc != 0)
		goto fop_fini;

	quit_dialog();

	if (verbose) {
		struct c2_reqh *reqh;

		reqh = c2_cs_reqh_get(&sctx.rsx_colibri_ctx, "ds1");
		if (reqh != NULL) {
			printf("########### Server DS1 statS ###########\n");
			print_stats(reqh);
		}

		reqh = c2_cs_reqh_get(&sctx.rsx_colibri_ctx, "ds2");
		if (reqh != NULL) {
			printf("\n");
			printf("########### Server DS2 statS ###########\n");
			print_stats(reqh);
		}
	}

	c2_rpc_server_stop(&sctx);
fop_fini:
	c2_ping_fop_fini();
c2_fini:
	c2_fini();
	return rc;
}
#endif

#ifdef __KERNEL__
int c2_rpc_ping_init()
#else
/* Main function for rpc ping */
int main(int argc, char *argv[])
#endif
{
	int rc;

	c2_addb_choose_default_level(AEL_WARN);

#ifndef __KERNEL__
	rc = C2_GETOPTS("rpcping", argc, argv,
		C2_FLAGARG('s', "run server", &server_mode),
		C2_STRINGARG('C', "client nid",
			LAMBDA(void, (const char *str) { client_nid =
								(char*)str; })),
		C2_FORMATARG('p', "client tmid", "%i", &client_tmid),
		C2_STRINGARG('S', "server nid",
			LAMBDA(void, (const char *str) { server_nid =
								(char*)str; })),
		C2_FORMATARG('P', "server tmid", "%i", &server_tmid),
		C2_FORMATARG('b', "size in bytes", "%i", &nr_ping_bytes),
		C2_FORMATARG('t', "number of client threads", "%i",
						&nr_client_threads),
		C2_FORMATARG('l', "number of slots", "%i", &nr_slots),
		C2_FORMATARG('n', "number of ping items", "%i", &nr_ping_item),
		C2_FORMATARG('q', "minimum TM receive queue length", "%i",
						&tm_recv_queue_len),
		C2_FORMATARG('m', "max rpc msg size", "%i",
						&max_rpc_msg_size),
		C2_FLAGARG('v', "verbose", &verbose)
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

void c2_rpc_ping_fini(void)
{
}
