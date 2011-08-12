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
#include "net/bulk_sunrpc.h"
#include "net/bulk_emulation/st/ping.h"

/**
   @addtogroup net_st Networking System Test

  @{
 */

enum {
	DEF_BUFS = 20,

	PING_SERVER_SEGMENTS = 4,
	PING_SERVER_SEGMENT_SIZE = 16384,
	/* leave some room for overhead */
	MAX_PASSIVE_SIZE =
		PING_SERVER_SEGMENTS * PING_SERVER_SEGMENT_SIZE - 1024,

	ONE_MILLION = 1000000ULL,
	SEC_PER_HR = 60 * 60,
	SEC_PER_MIN = 60,
};

/* Module parameters */
bool verbose = false;
module_param(verbose, bool, S_IRUGO);
MODULE_PARM_DESC(verbose, "enable verbose output to kernel log");
char *hostaddr = "127.0.0.1";
module_param(hostaddr, charp, S_IRUGO);
MODULE_PARM_DESC(hostaddr, "address to register as the server endpoint");
uint nr_bufs = DEF_BUFS;
module_param(nr_bufs, uint, S_IRUGO);
MODULE_PARM_DESC(nr_bufs, "total number of network buffers to allocate");
uint passive_size = 0;
module_param(passive_size, uint, S_IRUGO);
MODULE_PARM_DESC(passive_size, "size to offer for passive recv message");
int sunrpc_ep_delay = -1;
module_param(sunrpc_ep_delay, int, S_IRUGO);
MODULE_PARM_DESC(sunrpc_ep_delay,
	"Control how long unused end points are cached before release");
int active_bulk_delay = 0;
module_param(active_bulk_delay, int, S_IRUGO);
MODULE_PARM_DESC(active_bulk_delay, "Delay before sending active receive");
uint sunrpc_skulker_period = 0;
module_param(sunrpc_skulker_period, uint, S_IRUGO);
MODULE_PARM_DESC(sunrpc_skulker_period,
		 "Control the period of the skulker thread clock");

int quiet_printk(const char *fmt, ...)
{
	return 0;
}

int verbose_printk(const char *fmt, ...)
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

struct c2_mutex qstats_mutex;

void print_qstats(struct ping_ctx *ctx, bool reset)
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
"Queue   #Add   #Del  #Succ  #Fail Time in Queue   Total Bytes   Max Buffer Sz\n";
	const char *hfmt2 =
"----- ------ ------ ------ ------ ------------- --------------- -------------\n";

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

struct ping_ops verbose_ops = {
	.pf  = verbose_printk,
	.pqs = print_qstats
};

struct ping_ops quiet_ops = {
	.pf  = quiet_printk,
	.pqs = print_qstats
};

struct ping_ctx sctx = {
	.pc_tm = {
		.ntm_state     = C2_NET_TM_UNDEFINED
	}
};

struct c2_thread server_thread;

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
	if (in_aton(hostaddr) == 0) {
		printk(KERN_WARNING
		       "only dotted ipaddr is accepted: e.g. 1.2.3.4\n");
		return -EINVAL;
	}

	/* set up sys fs entries? */

	/* init main context */
	rc = c2_net_xprt_init(&c2_net_bulk_sunrpc_xprt);
	C2_ASSERT(rc == 0);
	c2_mutex_init(&qstats_mutex);

	/* set up server context */
	c2_mutex_init(&sctx.pc_mutex);
	c2_cond_init(&sctx.pc_cond);
	if (verbose)
	    sctx.pc_ops = &verbose_ops;
	else
	    sctx.pc_ops = &quiet_ops;
	sctx.pc_hostname = hostaddr;
	sctx.pc_xprt = &c2_net_bulk_sunrpc_xprt;
	sctx.pc_port = PING_PORT1;
	sctx.pc_id = PART3_SERVER_ID;

	sctx.pc_nr_bufs = nr_bufs;
	sctx.pc_segments = PING_SERVER_SEGMENTS;
	sctx.pc_seg_size = PING_SERVER_SEGMENT_SIZE;
	sctx.pc_passive_size = passive_size;
	sctx.pc_sunrpc_ep_delay = sunrpc_ep_delay;
	sctx.pc_server_bulk_delay = active_bulk_delay;
	sctx.pc_sunrpc_skulker_period = sunrpc_skulker_period;

	/* spawn server */
	C2_SET0(&server_thread);
	rc = C2_THREAD_INIT(&server_thread, struct ping_ctx *, NULL,
			    &ping_server, &sctx, "ping_server");
	C2_ASSERT(rc == 0);

	printk(KERN_INFO "Colibri Kernel Messaging System Test initialized\n");
	return 0;
}

static void __exit c2_netst_fini_k(void)
{
	if (sctx.pc_ops->pqs != NULL)
		(*sctx.pc_ops->pqs)(&sctx, false);

	ping_server_should_stop(&sctx);
	c2_thread_join(&server_thread);
	c2_cond_fini(&sctx.pc_cond);
	c2_mutex_fini(&sctx.pc_mutex);

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
