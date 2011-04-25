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
	DEF_LOOPS = 1,

	PING_CLIENT_SEGMENTS = 8,
	PING_CLIENT_SEGMENT_SIZE = 512,

	PING_SERVER_SEGMENTS = 4,
	PING_SERVER_SEGMENT_SIZE = 1024,

	CLIENT_BASE_PORT = 31416,
};

struct c2_net_xprt *xprts[3] = {
	&c2_net_bulk_mem_xprt,
	&c2_net_bulk_sunrpc_xprt,
	NULL
};

struct ping_ctx sctx = {
	.pc_tm = {
		.ntm_state     = C2_NET_TM_UNDEFINED
	}
};

int lookup_xprt(const char *xprt_name, struct c2_net_xprt **xprt)
{
	int i;

	for (i = 0; xprts[i] != NULL; ++i)
		if (strcmp(xprt_name, xprts[i]->nx_name) == 0) {
			*xprt = xprts[i];
			return 0;
		}
	return -ENOENT;
}

void list_xprt_names(FILE *s, struct c2_net_xprt *def)
{
	int i;

	fprintf(s, "Supported transports:\n");
	for (i = 0; xprts[i] != NULL; ++i)
		fprintf(s, "    %s%s\n", xprts[i]->nx_name,
			(xprts[i] == def) ? " [default]" : "");
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
	struct c2_net_xprt *xprt;
	bool verbose;
	int loops;
	int nr_bufs;
	int client_id;
};

void client(struct client_params *params)
{
	int			 i;
	int			 rc;
	struct c2_net_end_point *server_ep;
	char                     ident[16];
	struct ping_ctx		 cctx = {
		.pc_xprt = params->xprt,
		.pc_port = CLIENT_BASE_PORT + params->client_id,
		.pc_nr_bufs = params->nr_bufs,
		.pc_segments = PING_CLIENT_SEGMENTS,
		.pc_seg_size = PING_CLIENT_SEGMENT_SIZE,
		.pc_ident = ident,
		.pc_tm = {
			.ntm_state     = C2_NET_TM_UNDEFINED
		}
	};

	sprintf(ident, "Client %d", params->client_id);
	if (params->verbose)
		cctx.pc_ops = &verbose_ops;
	else
		cctx.pc_ops = &quiet_ops;
	c2_mutex_init(&cctx.pc_mutex);
	c2_cond_init(&cctx.pc_cond);
	rc = ping_client_init(&cctx, &server_ep);
	C2_ASSERT(rc == 0);

	for (i = 1; i <= params->loops; ++i) {
		if (params->verbose)
			printf("%s: Loop %d\n", ident, i);
		rc = ping_client_msg_send_recv(&cctx, server_ep, NULL);
		C2_ASSERT(rc == 0);
		rc = ping_client_passive_recv(&cctx, server_ep);
		C2_ASSERT(rc == 0);
		rc = ping_client_passive_send(&cctx, server_ep);
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
	bool			 interact = false;
	bool			 verbose = false;
	const char		*xprt_name = c2_net_bulk_mem_xprt.nx_name;
	int			 loops = DEF_LOOPS;
	int			 nr_clients = DEF_CLIENT_THREADS;
	int			 nr_bufs = DEF_BUFS;

	struct c2_net_xprt	*xprt;
	struct c2_thread	 server_thread;

	rc = c2_init();
	C2_ASSERT(rc == 0);

	rc = C2_GETOPTS("bulkping", argc, argv,
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
		list_xprt_names(stdout, &c2_net_bulk_mem_xprt);
		return 0;
	}
	rc = lookup_xprt(xprt_name, &xprt);
	if (rc != 0) {
		fprintf(stderr, "Unknown transport-name.\n");
		list_xprt_names(stderr, &c2_net_bulk_mem_xprt);
		return rc;
	}

	C2_ASSERT(c2_net_xprt_init(xprt) == 0);

	/* start server in background thread */
	c2_mutex_init(&sctx.pc_mutex);
	c2_cond_init(&sctx.pc_cond);
	if (verbose)
		sctx.pc_ops = &verbose_ops;
	else
		sctx.pc_ops = &quiet_ops;
	sctx.pc_xprt = xprt;
	sctx.pc_nr_bufs = nr_bufs;
	sctx.pc_segments = PING_SERVER_SEGMENTS;
	sctx.pc_seg_size = PING_SERVER_SEGMENT_SIZE;
	C2_SET0(&server_thread);
	rc = C2_THREAD_INIT(&server_thread, struct ping_ctx *, NULL,
			    &ping_server, &sctx);
	C2_ASSERT(rc == 0);

	int		      i;
	struct c2_thread     *client_thread;
	struct client_params *params;
	C2_ALLOC_ARR(client_thread, nr_clients);
	C2_ALLOC_ARR(params, nr_clients);

	/* start all the client threads */
	for (i = 0; i < nr_clients; ++i) {
		params[i].xprt = xprt;
		params[i].verbose = verbose;
		params[i].loops = loops;
		params[i].nr_bufs = nr_bufs;
		params[i].client_id = i;

		rc = C2_THREAD_INIT(&client_thread[i], struct client_params *,
				    NULL, &client, &params[i]);
		C2_ASSERT(rc == 0);
	}

	/* ...and wait for them */
	for (i = 0; i < nr_clients; ++i) {
		c2_thread_join(&client_thread[i]);
		if (verbose)
			printf("Client %d: joined\n", i);
	}
	c2_free(client_thread);
	c2_free(params);

	ping_server_should_stop(&sctx);
	c2_thread_join(&server_thread);
	c2_cond_fini(&sctx.pc_cond);
	c2_mutex_fini(&sctx.pc_mutex);

	c2_net_xprt_fini(xprt);
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
