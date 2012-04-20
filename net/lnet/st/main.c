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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "colibri/init.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/getopts.h"
#include "lib/memory.h"
#include "lib/thread.h"
#include "net/net.h"
#include "net/lnet/lnet.h"
#include "net/lnet/st/ping.h"

static struct nlx_ping_ctx sctx = {
	.pc_tm = {
		.ntm_state     = C2_NET_TM_UNDEFINED
	}
};

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
"%5s %6lu %6lu %6lu %6lu %13s %14lu %13lu\n";
	const char *hfmt =
"Queue   #Add   #Del  #Succ  #Fail Time in Queue   Total Bytes  "
" Max Buffer Sz\n"
"----- ------ ------ ------ ------ ------------- ---------------"
" -------------\n";

	if (ctx->pc_tm.ntm_state < C2_NET_TM_INITIALIZED)
		return;
	rc = c2_net_tm_stats_get(&ctx->pc_tm, C2_NET_QT_NR, qs, reset);
	C2_ASSERT(rc == 0);
	c2_mutex_lock(&qstats_mutex);
	ctx->pc_ops->pf("%s statistics:\n", ctx->pc_ident);
	ctx->pc_ops->pf("%s", hfmt);
	for (i = 0; i < ARRAY_SIZE(qs); ++i) {
		qp = &qs[i];
		sec = c2_time_seconds(qp->nqs_time_in_queue);
		hr = sec / SEC_PER_HR;
		min = sec % SEC_PER_HR / SEC_PER_MIN;
		sec %= SEC_PER_MIN;
		msec = (c2_time_nanoseconds(qp->nqs_time_in_queue) +
			ONE_MILLION / 2) / ONE_MILLION;
		sprintf(tbuf, "%02lu:%02lu:%02lu.%03lu",
			hr, min, sec, msec);
		ctx->pc_ops->pf(lfmt,
				qnames[i],
				qp->nqs_num_adds, qp->nqs_num_dels,
				qp->nqs_num_s_events, qp->nqs_num_f_events,
				tbuf, qp->nqs_total_bytes, qp->nqs_max_bytes);
	}
	c2_mutex_unlock(&qstats_mutex);
}

static int quiet_printf(const char *fmt, ...)
{
	return 0;
}

static struct nlx_ping_ops verbose_ops = {
	.pf  = printf,
	.pqs = print_qstats
};

static struct nlx_ping_ops quiet_ops = {
	.pf  = quiet_printf,
	.pqs = print_qstats
};

int main(int argc, char *argv[])
{
	int			 rc;
	bool			 client_only = false;
	bool			 server_only = false;
	bool			 verbose = false;
	int			 loops = PING_DEF_LOOPS;
	int			 nr_clients = PING_DEF_CLIENT_THREADS;
	int			 nr_bufs = PING_DEF_BUFS;
	int			 passive_size = 0;
	int                      bulk_timeout = PING_DEF_BULK_TIMEOUT;
	int                      msg_timeout = PING_DEF_MSG_TIMEOUT;
	int                      active_bulk_delay = 0;
	const char              *client_network = NULL;
        int32_t                  client_portal = -1;
	int32_t                  client_tmid = PING_CLIENT_DYNAMIC_TMID;
	const char              *server_network = NULL;
        int32_t                  server_portal = -1;
	int32_t                  server_tmid = -1;
	int                      client_debug = 0;
	int                      server_debug = 0;
	struct c2_thread	 server_thread;

	rc = c2_init();
	C2_ASSERT(rc == 0);

	rc = C2_GETOPTS("lnetping", argc, argv,
			C2_FLAGARG('s', "run server only", &server_only),
			C2_FLAGARG('c', "run client only", &client_only),
			C2_FORMATARG('b', "number of buffers", "%i", &nr_bufs),
			C2_FORMATARG('l', "loops to run", "%i", &loops),
			C2_FORMATARG('d', "passive data size", "%i",
				     &passive_size),
			C2_FORMATARG('n', "number of client threads", "%i",
				     &nr_clients),
			C2_FORMATARG('D', "server active bulk delay",
				     "%i", &active_bulk_delay),
			C2_STRINGARG('i', "client network interface (ip@intf)",
				     LAMBDA(void, (const char *str) {
						     client_network = str; })),
			C2_FORMATARG('p', "client portal (optional)",
				     "%i", &client_portal),
			C2_FORMATARG('t', "client base TMID (optional)",
				     "%i", &client_tmid),
			C2_STRINGARG('I', "server network interface (ip@intf)",
				     LAMBDA(void, (const char *str) {
						     server_network = str; })),
			C2_FORMATARG('P', "server portal (optional)",
				     "%i", &server_portal),
			C2_FORMATARG('T', "server TMID (optional)",
				     "%i", &server_tmid),
			C2_FORMATARG('o', "message timeout in seconds",
				     "%i", &msg_timeout),
			C2_FORMATARG('O', "bulk timeout in seconds",
				     "%i", &bulk_timeout),
			C2_FORMATARG('x', "client debug",
				     "%i", &client_debug),
			C2_FORMATARG('X', "server debug",
				     "%i", &server_debug),
			C2_FLAGARG('v', "verbose", &verbose));
	if (rc != 0)
		return rc;

	if (nr_clients > PING_MAX_CLIENT_THREADS) {
		fprintf(stderr, "Max of %d client threads supported\n",
			PING_MAX_CLIENT_THREADS);
		return 1;
	}
	if (nr_bufs < PING_MIN_BUFS) {
		fprintf(stderr, "Minimum of %d buffers required\n",
			PING_MIN_BUFS);
		return 1;
	}
	if (passive_size < 0 || passive_size >
	    (PING_CLIENT_SEGMENTS - 1) * PING_CLIENT_SEGMENT_SIZE) {
		/* need to leave room for encoding overhead */
		fprintf(stderr, "Max supported passive data size: %d\n",
			(PING_CLIENT_SEGMENTS - 1) * PING_CLIENT_SEGMENT_SIZE);
		return 1;
	}
	if (client_only && server_only)
		client_only = server_only = false;
	if (server_network == NULL) {
		fprintf(stderr, "Server LNet interface address missing ("
			"e.g. 10.1.2.3@tcp0, 1.2.3.4@o2ib1)\n");
		return 1;
	}
	if (!server_only && client_network == NULL) {
		fprintf(stderr, "Client LNet interface address missing ("
			"e.g. 10.1.2.3@tcp0, 1.2.3.4@o2ib1)\n");
		return 1;
	}
	if (server_portal < 0)
		server_portal = PING_SERVER_PORTAL;
	if (client_portal < 0)
		client_portal = PING_CLIENT_PORTAL;
	if (server_tmid < 0)
		server_tmid = PING_SERVER_TMID;
	if (client_tmid < 0)
		client_tmid = PING_CLIENT_DYNAMIC_TMID;

	C2_ASSERT(c2_net_xprt_init(&c2_net_lnet_xprt));
	c2_mutex_init(&qstats_mutex);

	if (!client_only) {
		/* start server in background thread */
		c2_mutex_init(&sctx.pc_mutex);
		c2_cond_init(&sctx.pc_cond);
		if (verbose)
			sctx.pc_ops = &verbose_ops;
		else
			sctx.pc_ops = &quiet_ops;
		sctx.pc_nr_bufs = nr_bufs;
		sctx.pc_segments = PING_SERVER_SEGMENTS;
		sctx.pc_seg_size = PING_SERVER_SEGMENT_SIZE;
		sctx.pc_passive_size = passive_size;
		sctx.pc_bulk_timeout = bulk_timeout;
		sctx.pc_msg_timeout = msg_timeout;
		sctx.pc_server_bulk_delay = active_bulk_delay;
		sctx.pc_network = server_network;
		sctx.pc_pid = C2_NET_LNET_PID;
		sctx.pc_portal = server_portal;
		sctx.pc_tmid = server_tmid;
		sctx.pc_dom_debug = server_debug;
		sctx.pc_tm_debug = server_debug;
		nlx_ping_server_spawn(&server_thread, &sctx);

		printf("Colibri LNet System Test Server Initialized\n");
	}

	if (server_only) {
		char readbuf[BUFSIZ];

		printf("Type \"quit\" or ^D to cause server to terminate\n");
		while (fgets(readbuf, BUFSIZ, stdin)) {
			if (strcmp(readbuf, "quit\n") == 0)
				break;
			if (strcmp(readbuf, "\n") == 0)
				print_qstats(&sctx, false);
			if (strcmp(readbuf, "reset_stats\n") == 0)
				print_qstats(&sctx, true);
		}
	} else {
		int		      i;
		struct c2_thread     *client_thread;
		struct nlx_ping_client_params *params;
		int32_t               client_base_tmid = client_tmid;
		C2_ALLOC_ARR(client_thread, nr_clients);
		C2_ALLOC_ARR(params, nr_clients);

		/* start all the client threads */
		for (i = 0; i < nr_clients; ++i) {
			if (client_base_tmid != PING_CLIENT_DYNAMIC_TMID)
				client_tmid = client_base_tmid + i;
#define CPARAM_SET(f) params[i].f = f
			CPARAM_SET(loops);
			CPARAM_SET(nr_bufs);
			CPARAM_SET(passive_size);
			CPARAM_SET(msg_timeout);
			CPARAM_SET(bulk_timeout);
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
		printf("Colibri LNet System Test %d Client(s) Initialized\n",
		       nr_clients);

		/* ...and wait for them */
		for (i = 0; i < nr_clients; ++i) {
			c2_thread_join(&client_thread[i]);
			if (verbose) {
				printf("Client %d: joined\n",
				       params[i].client_id);
			}
		}
		c2_free(client_thread);
		c2_free(params);
	}

	if (!client_only) {
		if (verbose)
			print_qstats(&sctx, false);
		nlx_ping_server_should_stop(&sctx);
		c2_thread_join(&server_thread);
		c2_cond_fini(&sctx.pc_cond);
		c2_mutex_fini(&sctx.pc_mutex);
	}

	c2_net_xprt_fini(&c2_net_lnet_xprt);
	c2_mutex_fini(&qstats_mutex);
	c2_fini();

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
