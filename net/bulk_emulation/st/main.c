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
#include "lib/misc.h" /* C2_SET0 */
#include "lib/thread.h"
#include "net/net.h"
#include "net/bulk_mem.h"
#include "net/bulk_sunrpc.h"
#include "ping.h"

enum {
	DEF_BUFS = 20,
	DEF_CLIENT_THREADS = 1,
	DEF_LOOPS = 1
};

struct c2_net_xprt *xprts[3] = {
	&c2_net_bulk_mem_xprt,
	&c2_net_bulk_sunrpc_xprt,
	NULL
};

struct ping_ops bulkping_ops = {
    .pf = printf
};

struct ping_ctx cctx = {
	.pc_tm = {
		.ntm_state     = C2_NET_TM_UNDEFINED
	}
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

int main(int argc, char *argv[])
{
	int			 rc;
	bool			 interact = false;
	bool			 verbose = false;
	const char		*xprt_name = c2_net_bulk_mem_xprt.nx_name;
	int			 loops = DEF_LOOPS;
	int			 nr_bufs = DEF_BUFS;

	struct c2_net_xprt	*xprt;
	struct c2_net_end_point *server_ep;
	struct c2_thread	 server_thread;

	rc = c2_init();
	C2_ASSERT(rc == 0);

	rc = C2_GETOPTS("bulkping", argc, argv,
			C2_FLAGARG('i', "interactive client mode", &interact),
			C2_FORMATARG('l', "loops to run", "%i", &loops),
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
	sctx.pc_ops = &bulkping_ops;
	sctx.pc_xprt = xprt;
	sctx.pc_nr_bufs = nr_bufs;
	C2_SET0(&server_thread);
	rc = C2_THREAD_INIT(&server_thread, struct ping_ctx *, NULL,
			    &ping_server, &sctx);
	C2_ASSERT(rc == 0);
	c2_mutex_init(&cctx.pc_mutex);
	c2_cond_init(&cctx.pc_cond);
	cctx.pc_ops = &bulkping_ops;
	cctx.pc_xprt = xprt;
	cctx.pc_nr_bufs = nr_bufs;
	rc = ping_client_init(&cctx, &server_ep);
	C2_ASSERT(rc == 0);

	int i;
	for (i = 1; i <= loops; ++i) {
		printf("Client: Loop %d\n", i);
		ping_client_msg_send_recv(&cctx, server_ep);
		ping_client_passive_recv(&cctx, server_ep);
		ping_client_passive_send(&cctx, server_ep);
	}

	rc = ping_client_fini(&cctx, server_ep);
	C2_ASSERT(rc == 0);
	c2_cond_fini(&cctx.pc_cond);
	c2_mutex_fini(&cctx.pc_mutex);

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
