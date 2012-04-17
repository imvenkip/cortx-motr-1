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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 04/12/2011
 * Adapted for LNet: 04/11/2012
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/inet.h> /* in_aton */

#include "lib/memory.h"
#include "lib/misc.h" /* C2_SET0 */
#include "net/net.h"
#include "net/lnet/lnet.h"
#include "net/lnet/st/ping.h"

/**
   @addtogroup LNetDFS LNet Transport System Test

  @{
 */

/* Module parameters */
static bool verbose = false;
module_param(verbose, bool, S_IRUGO);
MODULE_PARM_DESC(verbose, "enable verbose output to kernel log");

static bool client_only = false;
module_param(client_only, bool, S_IRUGO);
MODULE_PARM_DESC(verbose, "run client only");

static bool server_only = false;
module_param(server_only, bool, S_IRUGO);
MODULE_PARM_DESC(verbose, "run server only");

static uint nr_bufs = PING_DEF_BUFS;
module_param(nr_bufs, uint, S_IRUGO);
MODULE_PARM_DESC(nr_bufs, "total number of network buffers to allocate");

static uint passive_size = 0;
module_param(passive_size, uint, S_IRUGO);
MODULE_PARM_DESC(passive_size, "size to offer for passive recv message");

static int active_bulk_delay = 0;
module_param(active_bulk_delay, int, S_IRUGO);
MODULE_PARM_DESC(active_bulk_delay, "Delay before sending active receive");

static int nr_clients = PING_DEF_CLIENT_THREADS;
module_param(nr_clients, int, S_IRUGO);
MODULE_PARM_DESC(nr_clients, "number of client threads");

static int loops = PING_DEF_LOOPS;
module_param(loops, int, S_IRUGO);
MODULE_PARM_DESC(loops, "loops to run");

static int bulk_timeout = PING_DEF_BULK_TIMEOUT;
module_param(bulk_timeout, int, S_IRUGO);
MODULE_PARM_DESC(passive_bulk_timeout, "bulk timeout");

static int msg_timeout = PING_DEF_MSG_TIMEOUT;
module_param(msg_timeout, int, S_IRUGO);
MODULE_PARM_DESC(msg_timeout, "message timeout");

static char *client_network = NULL;
module_param(client_network, charp, S_IRUGO);
MODULE_PARM_DESC(client_network, "client network interface (ip@intf)");

static int client_portal = -1;
module_param(client_portal, int, S_IRUGO);
MODULE_PARM_DESC(client_portal, "client portal (optional)");

static int client_tmid = PING_CLIENT_DYNAMIC_TMID;
module_param(client_tmid, int, S_IRUGO);
MODULE_PARM_DESC(client_tmid, "client base TMID (optional)");

static char *server_network = NULL;
module_param(server_network, charp, S_IRUGO);
MODULE_PARM_DESC(server_network, "server network interface (ip@intf)");

static int server_portal = -1;
module_param(server_portal, int, S_IRUGO);
MODULE_PARM_DESC(server_portal, "server portal (optional)");

static int server_tmid = -1;
module_param(server_tmid, int, S_IRUGO);
MODULE_PARM_DESC(server_tmid, "server TMID (optional)");

static int server_debug = 0;
module_param(server_debug, int, S_IRUGO);
MODULE_PARM_DESC(server_debug, "server debug (optional)");

static int client_debug = 0;
module_param(client_debug, int, S_IRUGO);
MODULE_PARM_DESC(client_debug, "client debug (optional)");

static int quiet_printk(const char *fmt, ...)
{
	return 0;
}

static int verbose_printk(const char *fmt, ...)
{
	va_list varargs;
	char *fmtbuf;
	int rc;

	va_start(varargs, fmt);
	fmtbuf = c2_alloc(strlen(fmt) + sizeof KERN_INFO);
	if (fmtbuf != NULL) {
	    sprintf(fmtbuf, "%s%s", KERN_INFO, fmt);
	    fmt = fmtbuf;
	}
	/* call vprintk(KERN_INFO ...) */
	rc = vprintk(fmt, varargs);
	va_end(varargs);
	c2_free(fmtbuf);
	return rc;
}

static struct c2_mutex qstats_mutex;

static void print_qstats(struct nlx_ping_ctx *ctx, bool reset)
{
	int i;
	int rc;
	uint64_t hr;
	uint64_t min;
	uint64_t sec;
	uint64_t msec;
	struct c2_net_qstats qs[C2_NET_QT_NR];
	struct c2_net_qstats *qp;
	static const char *qnames[C2_NET_QT_NR] = {
		"mRECV", "mSEND",
		"pRECV", "pSEND",
		"aRECV", "aSEND",
	};
	char tbuf[16];
	const char *lfmt =
"%5s %6llu %6llu %6llu %6llu %13s %14llu %13llu\n";
	const char *hfmt1 =
"Queue   #Add   #Del  #Succ  #Fail Time in Queue   Total Bytes  "
" Max Buffer Sz\n";
	const char *hfmt2 =
"----- ------ ------ ------ ------ ------------- ---------------"
" -------------\n";

	if (ctx->pc_tm.ntm_state < C2_NET_TM_INITIALIZED)
		return;
	rc = c2_net_tm_stats_get(&ctx->pc_tm, C2_NET_QT_NR, qs, reset);
	C2_ASSERT(rc == 0);
	c2_mutex_lock(&qstats_mutex);
	ctx->pc_ops->pf("%s statistics:\n", ctx->pc_ident);
	ctx->pc_ops->pf("%s", hfmt1);
	ctx->pc_ops->pf("%s", hfmt2);
	for (i = 0; i < ARRAY_SIZE(qs); ++i) {
		qp = &qs[i];
		sec = c2_time_seconds(qp->nqs_time_in_queue);
		hr = sec / SEC_PER_HR;
		min = sec % SEC_PER_HR / SEC_PER_MIN;
		sec %= SEC_PER_MIN;
		msec = (c2_time_nanoseconds(qp->nqs_time_in_queue) +
			ONE_MILLION / 2) / ONE_MILLION;
		sprintf(tbuf, "%02llu:%02llu:%02llu.%03llu",
			hr, min, sec, msec);
		ctx->pc_ops->pf(lfmt,
				qnames[i],
				qp->nqs_num_adds, qp->nqs_num_dels,
				qp->nqs_num_s_events, qp->nqs_num_f_events,
				tbuf, qp->nqs_total_bytes, qp->nqs_max_bytes);
	}
	c2_mutex_unlock(&qstats_mutex);
}

static struct nlx_ping_ops verbose_ops = {
	.pf  = verbose_printk,
	.pqs = print_qstats
};

static struct nlx_ping_ops quiet_ops = {
	.pf  = quiet_printk,
	.pqs = print_qstats
};

static struct nlx_ping_ctx sctx = {
	.pc_tm = {
		.ntm_state     = C2_NET_TM_UNDEFINED
	}
};

static struct c2_thread server_thread;
static struct c2_thread *client_thread;
static struct nlx_ping_client_params *params;

static int __init c2_netst_init_k(void)
{
	int rc;

	/* parse module options */
	if (nr_bufs < PING_MIN_BUFS) {
		printk(KERN_WARNING "Minimum of %d buffers required\n",
		       PING_MIN_BUFS);
		return -EINVAL;
	}
	if (passive_size < 0 || passive_size > PING_MAX_PASSIVE_SIZE) {
		/* need to leave room for encoding overhead */
		printk(KERN_WARNING "Max supported passive data size: %d\n",
		       PING_MAX_PASSIVE_SIZE);
		return -EINVAL;
	}
	if (nr_clients > PING_MAX_CLIENT_THREADS) {
		printk(KERN_WARNING "Max of %d client threads supported\n",
			PING_MAX_CLIENT_THREADS);
		return -EINVAL;
	}
	if (client_only && server_only)
		client_only = server_only = false;
	if (server_network == NULL) {
		printk(KERN_WARNING "Server LNet interface address missing ("
			"e.g. 10.1.2.3@tcp0, 1.2.3.4@o2ib1)\n");
		return -EINVAL;
	}
	if (!server_only && client_network == NULL) {
		printk(KERN_WARNING "Client LNet interface address missing ("
			"e.g. 10.1.2.3@tcp0, 1.2.3.4@o2ib1)\n");
		return -EINVAL;
	}
	if (server_portal < 0)
		server_portal = PING_SERVER_PORTAL;
	if (client_portal < 0)
		client_portal = PING_CLIENT_PORTAL;
	if (server_tmid < 0)
		server_tmid = PING_SERVER_TMID;
	if (client_tmid < 0)
		client_tmid = PING_CLIENT_DYNAMIC_TMID;

	/* set up sys fs entries? */

	/* init main context */
	rc = c2_net_xprt_init(&c2_net_lnet_xprt);
	C2_ASSERT(rc == 0);
	c2_mutex_init(&qstats_mutex);

	if (!client_only) {
		/* set up server context */
		c2_mutex_init(&sctx.pc_mutex);
		c2_cond_init(&sctx.pc_cond);
		if (verbose)
			sctx.pc_ops = &verbose_ops;
		else
			sctx.pc_ops = &quiet_ops;

		sctx.pc_nr_bufs = nr_bufs;
		sctx.pc_passive_size = passive_size;
		sctx.pc_msg_timeout = msg_timeout;
		sctx.pc_bulk_timeout = bulk_timeout;
		sctx.pc_server_bulk_delay = active_bulk_delay;
		sctx.pc_network = server_network;
		sctx.pc_portal = server_portal;
		sctx.pc_tmid = server_tmid;
		sctx.pc_dom_debug = server_debug;
		sctx.pc_tm_debug = server_debug;
		nlx_ping_server_spawn(&server_thread, &sctx);

		printk(KERN_INFO "Colibri LNet System Test"
		       " Server Initialized\n");
	}

	if (!server_only) {
		int	i;
		int32_t client_base_tmid = client_tmid;
		C2_ALLOC_ARR(client_thread, nr_clients);
		C2_ALLOC_ARR(params, nr_clients);

		C2_ASSERT(client_thread != NULL);
		C2_ASSERT(params != NULL);

		/* start all the client threads */
		for (i = 0; i < nr_clients; ++i) {
			if (client_base_tmid != PING_CLIENT_DYNAMIC_TMID)
				client_tmid = client_base_tmid + i;
#define CPARAM_SET(f) params[i].f = f
			CPARAM_SET(loops);
			CPARAM_SET(nr_bufs);
			CPARAM_SET(passive_size);
			CPARAM_SET(bulk_timeout);
			CPARAM_SET(msg_timeout);
			CPARAM_SET(client_network);
			CPARAM_SET(client_portal);
			CPARAM_SET(client_tmid);
			CPARAM_SET(server_network);
			CPARAM_SET(server_portal);
			CPARAM_SET(server_tmid);
#undef CPARAM_SET
			params[i].client_id = i + 1;
			params[i].client_pid = C2_NET_LNET_PID;
			params[i].server_pid = C2_NET_LNET_PID;
			params[i].debug = client_debug;
			if (verbose)
				params[i].ops = &verbose_ops;
			else
				params[i].ops = &quiet_ops;

			rc = C2_THREAD_INIT(&client_thread[i],
					    struct nlx_ping_client_params *,
					    NULL, &nlx_ping_client, &params[i],
					    "client_%d", params[i].client_id);
			C2_ASSERT(rc == 0);
		}
		printk(KERN_INFO "Colibri LNet System Test"
		       " %d Client(s) Initialized\n", nr_clients);
	}

	return 0;
}

static void __exit c2_netst_fini_k(void)
{
	if (!server_only) {
		int i;
		for (i = 0; i < nr_clients; ++i) {
			c2_thread_join(&client_thread[i]);
			if (verbose) {
				printk(KERN_INFO "Client %d: joined\n",
				       params[i].client_id);
			}
		}
		c2_free(client_thread);
		c2_free(params);
	}

	if (!client_only) {
		if (sctx.pc_ops->pqs != NULL)
			(*sctx.pc_ops->pqs)(&sctx, false);

		nlx_ping_server_should_stop(&sctx);
		c2_thread_join(&server_thread);
		c2_cond_fini(&sctx.pc_cond);
		c2_mutex_fini(&sctx.pc_mutex);
	}

	printk(KERN_INFO "Colibri Kernel Messaging System Test removed\n");
}

module_init(c2_netst_init_k)
module_exit(c2_netst_fini_k)

MODULE_AUTHOR("Xyratex");
MODULE_DESCRIPTION("Colibri Kernel Messaging System Test");
/* GPL license required as long as kernel sunrpc is used */
MODULE_LICENSE("proprietary");

/** @} end of group net */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
