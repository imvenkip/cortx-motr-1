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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 04/12/2011
 */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/socket.h>
#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif
#include <arpa/inet.h>
#include <netdb.h>

#include "colibri/init.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/getopts.h"
#include "lib/memory.h"
#include "lib/misc.h" /* C2_SET0 */
#include "lib/thread.h"
#include "net/net.h"
#include "net/bulk_mem.h"
#include "ut/net.h" /* canon_host */
#include "ping.h"

enum {
	DEF_BUFS = 20,
	DEF_CLIENT_THREADS = 1,
	MAX_CLIENT_THREADS = 4196,
	DEF_LOOPS = 1,

	PING_CLIENT_SEGMENTS = 8,
	PING_CLIENT_SEGMENT_SIZE = 8192,

	PING_SERVER_SEGMENTS = 4,
	PING_SERVER_SEGMENT_SIZE = 16384,

	MEM_CLIENT_BASE_PORT = PING_PORT2,

	ONE_MILLION = 1000000ULL,
	SEC_PER_HR = 60 * 60,
	SEC_PER_MIN = 60,
};

struct ping_xprt {
	struct c2_net_xprt *px_xprt;
	bool                px_dual_only;
	bool                px_3part_addr;
	short               px_client_port;
};

struct ping_xprt xprts[1] = {
	{
		.px_xprt = &c2_net_bulk_mem_xprt,
		.px_dual_only = true,
		.px_3part_addr = false,
		.px_client_port = MEM_CLIENT_BASE_PORT,
	},
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
"Queue   #Add   #Del  #Succ  #Fail Time in Queue   Total Bytes   Max Buffer Sz\n"
"----- ------ ------ ------ ------ ------------- --------------- -------------\n";

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
	int base_port;
	int loops;
	int nr_bufs;
	int client_id;
	int passive_size;
	const char *local_host;
	const char *remote_host;
	int passive_bulk_timeout;
};

void client(struct client_params *params)
{
	int			 i;
	int			 rc;
	struct c2_net_end_point *server_ep;
	char                     ident[24];
	char			*bp = NULL;
	struct ping_ctx		 cctx = {
		.pc_xprt = params->xprt->px_xprt,
		.pc_hostname = params->local_host,
		.pc_rhostname = params->remote_host,
		.pc_rport = PING_PORT1,
		.pc_nr_bufs = params->nr_bufs,
		.pc_segments = PING_CLIENT_SEGMENTS,
		.pc_seg_size = PING_CLIENT_SEGMENT_SIZE,
		.pc_passive_size = params->passive_size,
		.pc_ident = ident,
		.pc_tm = {
			.ntm_state     = C2_NET_TM_UNDEFINED
		},
		.pc_passive_bulk_timeout = params->passive_bulk_timeout,
	};

	if (params->xprt->px_3part_addr) {
		cctx.pc_port = params->base_port;
		cctx.pc_id   = params->client_id;
		sprintf(ident, "Client %d:%d", cctx.pc_port, cctx.pc_id);
		cctx.pc_rid  = PART3_SERVER_ID;
	} else {
		cctx.pc_port = params->base_port + params->client_id;
		cctx.pc_id   = 0;
		cctx.pc_rid  = 0;
		sprintf(ident, "Client %d", cctx.pc_port);
	}
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
		cctx.pc_ops->pf("%s: Loop %d\n", ident, i);
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
	const char              *local_name = "localhost";
	const char              *remote_name = "localhost";
	const char		*xprt_name = c2_net_bulk_mem_xprt.nx_name;
	int			 loops = DEF_LOOPS;
	int			 base_port = 0;
	int			 nr_clients = DEF_CLIENT_THREADS;
	int			 nr_bufs = DEF_BUFS;
	int			 passive_size = 0;
	int                      passive_bulk_timeout = 0;
	int                      active_bulk_delay = 0;

	struct ping_xprt	*xprt;
	struct c2_thread	 server_thread;
	/* hostname buffers big enough for 255.255.255.255 */
	char                     local_hostbuf[16];
	char                     remote_hostbuf[16];

	rc = c2_init();
	C2_ASSERT(rc == 0);

	rc = C2_GETOPTS("bulkping", argc, argv,
			C2_FLAGARG('s', "run server only", &server_only),
			C2_FLAGARG('c', "run client only", &client_only),
			C2_STRINGARG('h', "hostname to listen on",
				     LAMBDA(void, (const char *str) {
						     local_name = str; })),
			C2_STRINGARG('r', "name of remote server host",
				     LAMBDA(void, (const char *str) {
						     remote_name = str; })),
			C2_FORMATARG('p', "base client port", "%i", &base_port),
			C2_FORMATARG('b', "number of buffers", "%i", &nr_bufs),
			C2_FORMATARG('l', "loops to run", "%i", &loops),
			C2_FORMATARG('d', "passive data size", "%i",
				     &passive_size),
			C2_FORMATARG('n', "number of client threads", "%i",
				     &nr_clients),
			C2_STRINGARG('t', "transport-name or \"list\" to "
				     "list supported transports.",
				     LAMBDA(void, (const char *str) {
						     xprt_name = str; })),
			C2_FORMATARG('D', "server active bulk delay",
				     "%i", &active_bulk_delay),
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
	if (xprt->px_dual_only && (client_only || server_only)) {
		fprintf(stderr,
			"Transport %s does not support client or server only\n",
			xprt_name);
		return 1;
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
	if (base_port == 0) {
		/* be nice and pick the non-server port by default */
		base_port = xprt->px_client_port;
		if (client_only && base_port == PING_PORT1)
			base_port = PING_PORT2;
	}
	if (canon_host(local_name, local_hostbuf, sizeof(local_hostbuf)) != 0)
		return 1;
	if (canon_host(remote_name, remote_hostbuf, sizeof(remote_hostbuf)) != 0)
		return 1;

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
		sctx.pc_hostname = local_hostbuf;
		sctx.pc_xprt = xprt->px_xprt;
		sctx.pc_port = PING_PORT1;
		if (xprt->px_3part_addr)
			sctx.pc_id = PART3_SERVER_ID;
		else
			sctx.pc_id = 0;
		sctx.pc_nr_bufs = nr_bufs;
		sctx.pc_segments = PING_SERVER_SEGMENTS;
		sctx.pc_seg_size = PING_SERVER_SEGMENT_SIZE;
		sctx.pc_passive_size = passive_size;
		sctx.pc_server_bulk_delay = active_bulk_delay;
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
		C2_ALLOC_ARR(client_thread, nr_clients);
		C2_ALLOC_ARR(params, nr_clients);

		/* start all the client threads */
		for (i = 0; i < nr_clients; ++i) {
			params[i].xprt = xprt;
			params[i].verbose = verbose;
			params[i].base_port = base_port;
			params[i].loops = loops;
			params[i].nr_bufs = nr_bufs;
			params[i].client_id = i + 1;
			params[i].passive_size = passive_size;
			params[i].local_host = local_hostbuf;
			params[i].remote_host = remote_hostbuf;
			params[i].passive_bulk_timeout = passive_bulk_timeout;

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
				if (xprt->px_3part_addr)
					printf("Client %d:%d: joined\n",
					       base_port, params[i].client_id);
				else
					printf("Client %d: joined\n",
					       base_port + params[i].client_id);
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
