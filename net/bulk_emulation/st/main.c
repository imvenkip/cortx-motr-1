/* -*- C -*- */
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
#include "net/bulk_mem.h"
#include "net/bulk_sunrpc.h"
#include "ping.h"

enum {
	DEF_BUFS = 20,
	DEF_CLIENT_THREADS = 1,
	MAX_CLIENT_THREADS = 4196,
	DEF_LOOPS = 1,

	PING_CLIENT_SEGMENTS = 8,
	PING_CLIENT_SEGMENT_SIZE = 512,

	PING_SERVER_SEGMENTS = 4,
	PING_SERVER_SEGMENT_SIZE = 1024,

	MEM_CLIENT_BASE_PORT = PING_PORT2,
	SUNRPC_CLIENT_BASE_PORT = PING_PORT1,
};

struct ping_xprt {
	struct c2_net_xprt *px_xprt;
	bool                px_dual_only;
	bool                px_3part_addr;
	short               px_client_port;
};

struct ping_xprt xprts[2] = {
	{
		.px_xprt = &c2_net_bulk_mem_xprt,
		.px_dual_only = true,
		.px_3part_addr = false,
		.px_client_port = MEM_CLIENT_BASE_PORT,
	},
	{
		.px_xprt = &c2_net_bulk_sunrpc_xprt,
		.px_dual_only = false,
		.px_3part_addr = true,
		.px_client_port = SUNRPC_CLIENT_BASE_PORT,
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

int quiet_printf(const char *fmt, ...)
{
	return 0;
}

struct ping_ops verbose_ops = {
    .pf = printf
};

struct ping_ops quiet_ops = {
    .pf = quiet_printf
};

struct client_params {
	struct ping_xprt *xprt;
	bool verbose;
	int base_port;
	int loops;
	int nr_bufs;
	int client_id;
};

void client(struct client_params *params)
{
	int			 i;
	int			 rc;
	struct c2_net_end_point *server_ep;
	char                     ident[24];
	struct ping_ctx		 cctx = {
		.pc_xprt = params->xprt->px_xprt,
		.pc_nr_bufs = params->nr_bufs,
		.pc_segments = PING_CLIENT_SEGMENTS,
		.pc_seg_size = PING_CLIENT_SEGMENT_SIZE,
		.pc_ident = ident,
		.pc_tm = {
			.ntm_state     = C2_NET_TM_UNDEFINED
		}
	};

	if (params->xprt->px_3part_addr) {
		cctx.pc_port = params->base_port;
		cctx.pc_id   = params->client_id;
		sprintf(ident, "Client %d:%d", cctx.pc_port, cctx.pc_id);
	} else {
		cctx.pc_port = params->base_port + params->client_id;
		cctx.pc_id = 0;
		sprintf(ident, "Client %d", cctx.pc_port);
	}
	if (params->verbose)
		cctx.pc_ops = &verbose_ops;
	else
		cctx.pc_ops = &quiet_ops;
	c2_mutex_init(&cctx.pc_mutex);
	c2_cond_init(&cctx.pc_cond);
	rc = ping_client_init(&cctx, &server_ep);
	C2_ASSERT(rc == 0);

	for (i = 1; i <= params->loops; ++i) {
		cctx.pc_ops->pf("%s: Loop %d\n", ident, i);
		rc = ping_client_msg_send_recv(&cctx, server_ep, NULL);
		C2_ASSERT(rc == 0);
		rc = ping_client_passive_recv(&cctx, server_ep);
		C2_ASSERT(rc == 0);
		rc = ping_client_passive_send(&cctx, server_ep, NULL);
		C2_ASSERT(rc == 0);
	}

	rc = ping_client_fini(&cctx, server_ep);
	C2_ASSERT(rc == 0);
	c2_cond_fini(&cctx.pc_cond);
	c2_mutex_fini(&cctx.pc_mutex);
}

int main(int argc, char *argv[])
{
	int			 rc;
	bool			 client_only = false;
	bool			 server_only = false;
	bool			 interact = false;
	bool			 verbose = false;
	const char		*xprt_name = c2_net_bulk_mem_xprt.nx_name;
	int			 loops = DEF_LOOPS;
	int			 base_port = 0;
	int			 nr_clients = DEF_CLIENT_THREADS;
	int			 nr_bufs = DEF_BUFS;

	struct ping_xprt	*xprt;
	struct c2_thread	 server_thread;

	rc = c2_init();
	C2_ASSERT(rc == 0);

	rc = C2_GETOPTS("bulkping", argc, argv,
			C2_FLAGARG('s', "run server only", &server_only),
			C2_FLAGARG('c', "run client only", &client_only),
			C2_FORMATARG('p', "base client port", "%i", &base_port),
			C2_FLAGARG('i', "interactive client mode", &interact),
			C2_FORMATARG('l', "loops to run", "%i", &loops),
			C2_FORMATARG('n', "number of client threads", "%i",
				     &nr_clients),
			C2_STRINGARG('t', "transport-name or \"list\" to "
				     "list supported transports.",
				     LAMBDA(void, (const char *str) {
						     xprt_name = str; })),
			C2_FLAGARG('v', "verbose", &verbose));
	if (rc != 0)
		return rc;
	if (interact) {
		fprintf(stderr, "Interactive client not yet implemented.\n");
		return 1;
	}

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
	if (client_only && server_only)
		client_only = server_only = false;
	if (base_port == 0)
		base_port = xprt->px_client_port;

	C2_ASSERT(c2_net_xprt_init(xprt->px_xprt) == 0);

	if (!client_only) {
		/* start server in background thread */
		c2_mutex_init(&sctx.pc_mutex);
		c2_cond_init(&sctx.pc_cond);
		if (verbose)
			sctx.pc_ops = &verbose_ops;
		else
			sctx.pc_ops = &quiet_ops;
		sctx.pc_xprt = xprt->px_xprt;
		sctx.pc_port = PING_PORT1;
		if (xprt->px_3part_addr)
			sctx.pc_id = PART3_SERVER_ID;
		else
			sctx.pc_id = 0;
		sctx.pc_nr_bufs = nr_bufs;
		sctx.pc_segments = PING_SERVER_SEGMENTS;
		sctx.pc_seg_size = PING_SERVER_SEGMENT_SIZE;
		C2_SET0(&server_thread);
		rc = C2_THREAD_INIT(&server_thread, struct ping_ctx *, NULL,
				    &ping_server, &sctx);
		C2_ASSERT(rc == 0);
	}

	if (server_only) {
		char readbuf[BUFSIZ];

		printf("Type \"quit\" or ^D to cause server to terminate\n");
		while (fgets(readbuf, BUFSIZ, stdin)) {
			if (strcmp(readbuf, "quit\n") == 0)
				break;
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

			rc = C2_THREAD_INIT(&client_thread[i],
					    struct client_params *,
					    NULL, &client, &params[i]);
			C2_ASSERT(rc == 0);
		}

		/* ...and wait for them */
		for (i = 0; i < nr_clients; ++i) {
			c2_thread_join(&client_thread[i]);
			if (verbose)
				printf("Client %d: joined\n",
				       base_port + params[i].client_id);
		}
		c2_free(client_thread);
		c2_free(params);
	}

	if (!client_only) {
		ping_server_should_stop(&sctx);
		c2_thread_join(&server_thread);
		c2_cond_fini(&sctx.pc_cond);
		c2_mutex_fini(&sctx.pc_mutex);
	}

	c2_net_xprt_fini(xprt->px_xprt);
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
