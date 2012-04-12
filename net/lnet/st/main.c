/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
#include "lib/misc.h" /* C2_SET0 */
#include "lib/thread.h"
#include "net/net.h"
#include "net/lnet/lnet.h"
#include "net/lnet/st/ping.h"

enum {
	DEF_BUFS = 20,
	DEF_CLIENT_THREADS = 1,
	MAX_CLIENT_THREADS = 4196,
	DEF_LOOPS = 1,

	PING_CLIENT_SEGMENTS = 8,
	PING_CLIENT_SEGMENT_SIZE = 8192,

	PING_SERVER_SEGMENTS = 4,
	PING_SERVER_SEGMENT_SIZE = 16384,

	ONE_MILLION = 1000000ULL,
	SEC_PER_HR = 60 * 60,
	SEC_PER_MIN = 60,
};

struct ping_xprt {
	struct c2_net_xprt *px_xprt;
};

struct ping_xprt xprts[1] = {
	{
		.px_xprt = &c2_net_lnet_xprt,
	}
};

struct ping_ctx sctx = {
	.pc_tm = {
		.ntm_state     = C2_NET_TM_UNDEFINED
	}
};

int lookup_xprt(const char *xprt_name, struct ping_xprt **xprt)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(xprts); ++i)
		if (strcmp(xprt_name, xprts[i].px_xprt->nx_name) == 0) {
			*xprt = &xprts[i];
			return 0;
		}
	return -ENOENT;
}

void list_xprt_names(FILE *s, struct ping_xprt *def)
{
	int i;

	fprintf(s, "Supported transports:\n");
	for (i = 0; ARRAY_SIZE(xprts); ++i)
		fprintf(s, "    %s%s\n", xprts[i].px_xprt->nx_name,
			(&xprts[i] == def) ? " [default]" : "");
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

int quiet_printf(const char *fmt, ...)
{
	return 0;
}

struct ping_ops verbose_ops = {
	.pf  = printf,
	.pqs = print_qstats
};

struct ping_ops quiet_ops = {
	.pf  = quiet_printf,
	.pqs = print_qstats
};

struct client_params {
	struct ping_xprt *xprt;
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

void client(struct client_params *params)
{
	int			 i;
	int			 rc;
	struct c2_net_end_point *server_ep;
	char			*bp = NULL;
	struct ping_ctx		 cctx = {
		.pc_xprt = params->xprt->px_xprt,
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
	rc = ping_client_init(&cctx, &server_ep);
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
		rc = ping_client_msg_send_recv(&cctx, server_ep, bp);
		C2_ASSERT(rc == 0);
		rc = ping_client_passive_recv(&cctx, server_ep);
		C2_ASSERT(rc == 0);
		rc = ping_client_passive_send(&cctx, server_ep, bp);
		C2_ASSERT(rc == 0);
	}

	if (params->verbose)
		print_qstats(&cctx, false);
	rc = ping_client_fini(&cctx, server_ep);
	c2_free(bp);
	C2_ASSERT(rc == 0);
fail:
	c2_cond_fini(&cctx.pc_cond);
	c2_mutex_fini(&cctx.pc_mutex);
}

int main(int argc, char *argv[])
{
	int			 rc;
	bool			 client_only = false;
	bool			 server_only = false;
	bool			 verbose = false;
	const char		*xprt_name = c2_net_lnet_xprt.nx_name;
	int			 loops = DEF_LOOPS;
	int			 nr_clients = DEF_CLIENT_THREADS;
	int			 nr_bufs = DEF_BUFS;
	int			 passive_size = 0;
	int                      passive_bulk_timeout = 0;
	int                      active_bulk_delay = 0;
	const char              *client_network = NULL;
        int32_t                  client_portal = -1;
	int32_t                  client_tmid = PING_CLIENT_DYNAMIC_TMID;
	const char              *server_network = NULL;
        int32_t                  server_portal = -1;
	int32_t                  server_tmid = -1;
	struct ping_xprt	*xprt;
	struct c2_thread	 server_thread;

	rc = c2_init();
	C2_ASSERT(rc == 0);

	rc = C2_GETOPTS("bulkping", argc, argv,
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
			C2_FLAGARG('v', "verbose", &verbose));
	if (rc != 0)
		return rc;

	if (strcmp(xprt_name, "list") == 0) {
		list_xprt_names(stdout, &xprts[0]);
		return 0;
	}
	rc = lookup_xprt(xprt_name, &xprt);
	if (rc != 0) {
		fprintf(stderr, "Unknown transport-name.\n");
		list_xprt_names(stderr, &xprts[0]);
		return rc;
	}
	if (nr_clients > MAX_CLIENT_THREADS) {
		fprintf(stderr, "Max of %d client threads supported\n",
			MAX_CLIENT_THREADS);
		return 1;
	}
	if (nr_bufs < DEF_BUFS) {
		fprintf(stderr, "Minimum of %d buffers required\n", DEF_BUFS);
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

	C2_ASSERT(c2_net_xprt_init(xprt->px_xprt) == 0);
	c2_mutex_init(&qstats_mutex);

	if (!client_only) {
		/* start server in background thread */
		c2_mutex_init(&sctx.pc_mutex);
		c2_cond_init(&sctx.pc_cond);
		if (verbose)
			sctx.pc_ops = &verbose_ops;
		else
			sctx.pc_ops = &quiet_ops;
		sctx.pc_xprt = xprt->px_xprt;
		sctx.pc_nr_bufs = nr_bufs;
		sctx.pc_segments = PING_SERVER_SEGMENTS;
		sctx.pc_seg_size = PING_SERVER_SEGMENT_SIZE;
		sctx.pc_passive_size = passive_size;
		sctx.pc_server_bulk_delay = active_bulk_delay;
		sctx.pc_network = server_network;
		sctx.pc_pid = C2_NET_LNET_PID;
		sctx.pc_portal = server_portal;
		sctx.pc_tmid = server_tmid;
		C2_SET0(&server_thread);
		rc = C2_THREAD_INIT(&server_thread, struct ping_ctx *, NULL,
				    &ping_server, &sctx, "ping_server");
		C2_ASSERT(rc == 0);
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
		struct client_params *params;
		int32_t               client_base_tmid = client_tmid;
		C2_ALLOC_ARR(client_thread, nr_clients);
		C2_ALLOC_ARR(params, nr_clients);

		/* start all the client threads */
		for (i = 0; i < nr_clients; ++i) {
			if (client_base_tmid != PING_CLIENT_DYNAMIC_TMID)
				client_tmid = client_base_tmid + i;
#define CPARAM_SET(f) params[i].f = f
			CPARAM_SET(xprt);
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
		ping_server_should_stop(&sctx);
		c2_thread_join(&server_thread);
		c2_cond_fini(&sctx.pc_cond);
		c2_mutex_fini(&sctx.pc_mutex);
	}

	c2_net_xprt_fini(xprt->px_xprt);
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
