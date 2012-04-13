/* -*- C -*- */
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

enum {
	DEF_BUFS = 20,
	DEF_CLIENT_THREADS = 1,
	MAX_CLIENT_THREADS = 32,
	DEF_LOOPS = 1,

	/* cannot allocate too many buffers in the kernel */
	PING_CLIENT_SEGMENTS = 8,
	PING_CLIENT_SEGMENT_SIZE = 4096,
	PING_SERVER_SEGMENTS = 8,
	PING_SERVER_SEGMENT_SIZE = 4096,
	/* leave some room for overhead */
	MAX_PASSIVE_SIZE =
		PING_SERVER_SEGMENTS * PING_SERVER_SEGMENT_SIZE - 1024,

	ONE_MILLION = 1000000ULL,
	SEC_PER_HR = 60 * 60,
	SEC_PER_MIN = 60,
};

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

static uint nr_bufs = DEF_BUFS;
module_param(nr_bufs, uint, S_IRUGO);
MODULE_PARM_DESC(nr_bufs, "total number of network buffers to allocate");

static uint passive_size = 0;
module_param(passive_size, uint, S_IRUGO);
MODULE_PARM_DESC(passive_size, "size to offer for passive recv message");

static int active_bulk_delay = 0;
module_param(active_bulk_delay, int, S_IRUGO);
MODULE_PARM_DESC(active_bulk_delay, "Delay before sending active receive");

static int nr_clients = DEF_CLIENT_THREADS;
module_param(nr_clients, int, S_IRUGO);
MODULE_PARM_DESC(nr_clients, "number of client threads");

static int loops = DEF_LOOPS;
module_param(loops, int, S_IRUGO);
MODULE_PARM_DESC(loops, "loops to run");

static int passive_bulk_timeout = 0;
module_param(passive_bulk_timeout, int, S_IRUGO);
MODULE_PARM_DESC(passive_bulk_timeout, "passive bulk timeout");

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

static void print_qstats(struct c2_nlx_ping_ctx *ctx, bool reset)
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

static struct c2_nlx_ping_ops verbose_ops = {
	.pf  = verbose_printk,
	.pqs = print_qstats
};

static struct c2_nlx_ping_ops quiet_ops = {
	.pf  = quiet_printk,
	.pqs = print_qstats
};

static struct c2_nlx_ping_ctx sctx = {
	.pc_tm = {
		.ntm_state     = C2_NET_TM_UNDEFINED
	}
};

struct client_params {
	bool verbose;
	int loops;
	int nr_bufs;
	int client_id;
	int passive_size;
	int passive_bulk_timeout;
	const char *client_network;
	uint32_t    client_pid;
	uint32_t    client_portal;
	int32_t	    client_tmid;
	const char *server_network;
	uint32_t    server_pid;
	uint32_t    server_portal;
	int32_t	    server_tmid;
};

static void client(struct client_params *params)
{
	int			 i;
	int			 rc;
	struct c2_net_end_point *server_ep;
	char			*bp = NULL;
	struct c2_nlx_ping_ctx		 cctx = {
		.pc_xprt = &c2_net_lnet_xprt,
		.pc_nr_bufs = params->nr_bufs,
		.pc_segments = PING_CLIENT_SEGMENTS,
		.pc_seg_size = PING_CLIENT_SEGMENT_SIZE,
		.pc_passive_size = params->passive_size,
		.pc_tm = {
			.ntm_state     = C2_NET_TM_UNDEFINED
		},
		.pc_passive_bulk_timeout = params->passive_bulk_timeout,

		.pc_network = params->client_network,
		.pc_pid     = params->client_pid,
		.pc_portal  = params->client_portal,
		.pc_tmid    = params->client_tmid,

		.pc_rnetwork = params->server_network,
		.pc_rpid     = params->server_pid,
		.pc_rportal  = params->server_portal,
		.pc_rtmid    = params->server_tmid,
	};

	if (params->verbose)
		cctx.pc_ops = &verbose_ops;
	else
		cctx.pc_ops = &quiet_ops;
	c2_mutex_init(&cctx.pc_mutex);
	c2_cond_init(&cctx.pc_cond);
	rc = c2_nlx_ping_client_init(&cctx, &server_ep);
	if (rc != 0)
		goto fail;

	if (params->passive_size != 0) {
		bp = c2_alloc(params->passive_size);
		C2_ASSERT(bp != NULL);
		for (i = 0; i < params->passive_size - 1; ++i)
			bp[i] = "abcdefghi"[i % 9];
	}

	for (i = 1; i <= params->loops; ++i) {
		cctx.pc_ops->pf("%s: Loop %d\n", cctx.pc_ident, i);
		rc = c2_nlx_ping_client_msg_send_recv(&cctx, server_ep, bp);
		C2_ASSERT(rc == 0);
		rc = c2_nlx_ping_client_passive_recv(&cctx, server_ep);
		C2_ASSERT(rc == 0);
		rc = c2_nlx_ping_client_passive_send(&cctx, server_ep, bp);
		C2_ASSERT(rc == 0);
	}

	if (params->verbose)
		print_qstats(&cctx, false);
	rc = c2_nlx_ping_client_fini(&cctx, server_ep);
	c2_free(bp);
	C2_ASSERT(rc == 0);
fail:
	c2_cond_fini(&cctx.pc_cond);
	c2_mutex_fini(&cctx.pc_mutex);
}

static struct c2_thread server_thread;
static struct c2_thread     *client_thread;
static struct client_params *params;

static int __init c2_netst_init_k(void)
{
	int rc;

	/* parse module options */
	if (nr_bufs < DEF_BUFS) {
		printk(KERN_WARNING "Minimum of %d buffers required\n",
		       DEF_BUFS);
		return -EINVAL;
	}
	if (passive_size < 0 || passive_size > MAX_PASSIVE_SIZE) {
		/* need to leave room for encoding overhead */
		printk(KERN_WARNING "Max supported passive data size: %d\n",
		       MAX_PASSIVE_SIZE);
		return -EINVAL;
	}
	if (nr_clients > MAX_CLIENT_THREADS) {
		printk(KERN_WARNING "Max of %d client threads supported\n",
			MAX_CLIENT_THREADS);
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
		sctx.pc_xprt = &c2_net_lnet_xprt;

		sctx.pc_nr_bufs = nr_bufs;
		sctx.pc_segments = PING_SERVER_SEGMENTS;
		sctx.pc_seg_size = PING_SERVER_SEGMENT_SIZE;
		sctx.pc_passive_size = passive_size;
		sctx.pc_server_bulk_delay = active_bulk_delay;
		sctx.pc_network = server_network;
		sctx.pc_pid = C2_NET_LNET_PID;
		sctx.pc_portal = server_portal;
		sctx.pc_tmid = server_tmid;

		/* spawn server */
		C2_SET0(&server_thread);
		rc = C2_THREAD_INIT(&server_thread, struct c2_nlx_ping_ctx *,
				    NULL, &c2_nlx_ping_server, &sctx,
				    "ping_server");
		C2_ASSERT(rc == 0);
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
			CPARAM_SET(verbose);
			CPARAM_SET(loops);
			CPARAM_SET(nr_bufs);
			CPARAM_SET(passive_size);
			CPARAM_SET(passive_bulk_timeout);
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

			rc = C2_THREAD_INIT(&client_thread[i],
					    struct client_params *,
					    NULL, &client, &params[i],
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

		c2_nlx_ping_server_should_stop(&sctx);
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
