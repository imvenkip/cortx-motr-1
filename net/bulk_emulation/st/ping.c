/* -*- C -*- */
#ifdef HAVE_CONFIG_H
#  include <config.h>
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
#include "lib/misc.h"
#include "lib/thread.h"
#include "net/net.h"
#include "net/bulk_mem.h"
#include "net/bulk_sunrpc.h"

enum {
	DEF_BUFS = 20,
	DEF_CLIENT_THREADS = 1,
	DEF_LOOPS = 1,

	PING_BUFSIZE = 4096,
	PING_SEGMENTS = 4,
	PING_SEGMENT_SIZE = 1024,

	CLIENT_PORT = 31416,
	SERVER_PORT = 27183
};

struct c2_net_xprt *xprts[3] = {
	&c2_net_bulk_mem_xprt,
	&c2_net_bulk_sunrpc_xprt,
	NULL
};

int lookup_xprt(const char *xprt_name, struct c2_net_xprt **xprt)
{
	int i;

	for (i = 0; xprts[i] != NULL; ++i) {
		if (strcmp(xprt_name, xprts[i]->nx_name) == 0) {
			*xprt = xprts[i];
			return 0;
		}
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

int alloc_buffers(int num, uint32_t segs, c2_bcount_t segsize,
		  struct c2_net_buffer **out)
{
	struct c2_net_buffer *nbs, *nb;
	int                   i;
	int                   rc = 0;

	C2_ALLOC_ARR(nbs, num);
	if (nbs == NULL)
		return -ENOMEM;
	for (i = 0; i < num; ++i) {
		nb = &nbs[i];
		rc = c2_bufvec_alloc(&nb->nb_buffer, segs, segsize);
		if (rc != 0)
			break;
	}

	if (rc == 0)
		*out = nbs;
	else {
		while (--i >= 0)
			c2_bufvec_free(&nbs[i].nb_buffer);
		c2_free(nbs);
	}
	return rc;
}

/* client callbacks */
void c_m_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
}

void c_m_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
}

void c_p_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
}

void c_p_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
}

void c_a_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
}

void c_a_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
}

void c_event_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	printf("Client Event CB: type == %d\n", ev->nev_qtype);
}

struct c2_net_tm_callbacks ctm_cb = {
	.ntc_msg_recv_cb	  = c_m_recv_cb,
	.ntc_msg_send_cb	  = c_m_send_cb,
	.ntc_passive_bulk_recv_cb = c_p_recv_cb,
	.ntc_passive_bulk_send_cb = c_p_send_cb,
	.ntc_active_bulk_recv_cb  = c_a_recv_cb,
	.ntc_active_bulk_send_cb  = c_a_send_cb,
	.ntc_event_cb		  = c_event_cb
};

/* server callbacks */
void s_m_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
}

void s_m_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
}

void s_p_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
}

void s_p_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
}

void s_a_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
}

void s_a_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
}

void s_event_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	printf("Server Event CB: type == %d\n", ev->nev_qtype);
}

struct c2_net_tm_callbacks stm_cb = {
	.ntc_msg_recv_cb = s_m_recv_cb,
	.ntc_msg_send_cb = s_m_send_cb,
	.ntc_passive_bulk_recv_cb = s_p_recv_cb,
	.ntc_passive_bulk_send_cb = s_p_send_cb,
	.ntc_active_bulk_recv_cb = s_a_recv_cb,
	.ntc_active_bulk_send_cb = s_a_send_cb,
	.ntc_event_cb = s_event_cb
};

/**
   Context for a ping client or server.
 */
struct ping_ctx {
	struct c2_net_xprt         *pc_xprt;
	struct c2_net_domain        pc_dom;
	int                         pc_nr_bufs;
	struct c2_net_buffer       *pc_nbs;
	struct c2_net_end_point    *pc_ep;
	struct c2_net_transfer_mc   pc_tm;
	struct c2_atomic64	    pc_stop;
};

struct ping_ctx cctx = {
	.pc_tm = {
		.ntm_callbacks = &ctm_cb,
		.ntm_state     = C2_NET_TM_UNDEFINED
	}
};

struct ping_ctx sctx = {
	.pc_tm = {
		.ntm_callbacks = &stm_cb,
		.ntm_state     = C2_NET_TM_UNDEFINED
	}
};

void ping_fini(struct ping_ctx *ctx);

/**
   Initialise a ping client or server.
   Calls all the required c2_net APIs in the correct order, with
   cleanup on failure.
   On success, the transfer machine is started.
   @param ctx the client/server context.  pc_xprt, pc_nr_bufs and pc_tm
   must be initialised by the caller.
   @retval 0 success
   @retval -errno failure
 */
int ping_init(const char *hostname, short port, struct ping_ctx *ctx)
{
	int                i;
	int                rc;
	struct in_addr     ipaddr;
	char               hostbuf[16]; /* big enough for 255.255.255.255 */

	/* c2_net_end_point_create requires string IPv4 address, not name */
	if (inet_aton(hostname, &ipaddr) == 0) {
		struct hostent he;
		char he_buf[4096];
		struct hostent *hp;
		int herrno;

		rc = gethostbyname_r(hostname, &he, he_buf, sizeof he_buf,
				     &hp, &herrno);
		if (rc != 0) {
			fprintf(stderr, "Can't get address for %s\n",
				hostname);
			return -ENOENT;
		}
		for (i = 0; hp->h_addr_list[i] != NULL; ++i) {
			/* take 1st IPv4 address found */
			if (hp->h_addrtype == AF_INET &&
			    hp->h_length == sizeof(ipaddr))
				break;
		}
		if (hp->h_addr_list[i] == NULL) {
			fprintf(stderr, "No IPv4 address for %s\n",
				hostname);
			return -EPFNOSUPPORT;
		}
		hostname = inet_ntop(hp->h_addrtype, hp->h_addr, hostbuf, 16);
	}

	rc = c2_net_domain_init(&ctx->pc_dom, ctx->pc_xprt);
	if (rc != 0) {
		fprintf(stderr, "domain init failed: %d\n", rc);
		goto fail;
	}

	rc = alloc_buffers(ctx->pc_nr_bufs, PING_SEGMENTS, PING_SEGMENT_SIZE,
			   &ctx->pc_nbs);
	if (rc != 0) {
		fprintf(stderr, "buffer allocation failed: %d\n", rc);
		goto fail;
	}
	for (i = 0; i < ctx->pc_nr_bufs; ++i) {
		rc = c2_net_buffer_register(&ctx->pc_nbs[i], &ctx->pc_dom);
		if (rc != 0) {
			fprintf(stderr, "buffer register failed: %d\n", rc);
			goto fail;
		}
	}

	/* TODO: verify correct varargs for mem and sunrpc xprt */
	rc = c2_net_end_point_create(&ctx->pc_ep, &ctx->pc_dom,
				     hostname, port, 0);
	if (rc != 0) {
		fprintf(stderr, "end point create failed: %d\n", rc);
		goto fail;
	}

	rc = c2_net_tm_init(&ctx->pc_tm, &ctx->pc_dom);
	if (rc != 0) {
		fprintf(stderr, "transfer machine init failed: %d\n", rc);
		goto fail;
	}

	struct c2_clink tmwait;
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&ctx->pc_tm.ntm_chan, &tmwait);
	rc = c2_net_tm_start(&ctx->pc_tm, ctx->pc_ep);
	if (rc != 0) {
		fprintf(stderr, "transfer machine start failed: %d\n", rc);
		goto fail;
	}

	c2_atomic64_set(&ctx->pc_stop, 0);

	/* wait for tm to notify it has started */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	
	return 0;
fail:
	ping_fini(ctx);
	return rc;
}

void ping_fini(struct ping_ctx *ctx)
{
	if (ctx->pc_tm.ntm_state != C2_NET_TM_UNDEFINED) {
		struct c2_clink tmwait;
		struct c2_time delay, rem;
		c2_clink_init(&tmwait, NULL);
		c2_clink_add(&ctx->pc_tm.ntm_chan, &tmwait);

		c2_net_tm_stop(&ctx->pc_tm, true);
		c2_chan_wait(&tmwait); /* wait for it to stop */
		c2_clink_del(&tmwait);

		while (1) {
			if (ctx->pc_tm.ntm_state == C2_NET_TM_STOPPED &&
			    c2_net_tm_fini(&ctx->pc_tm) != -EBUSY)
				break;
			c2_time_set(&delay, 0, 1000L);
			c2_nanosleep(&delay, &rem);
		}
	}
	if (ctx->pc_ep != NULL)
		c2_net_end_point_put(ctx->pc_ep);
	if (ctx->pc_nbs != NULL) {
		int i;
		for (i = 0; i < ctx->pc_nr_bufs; ++i) {
			struct c2_net_buffer *nb = &ctx->pc_nbs[i];
			if (nb->nb_flags == C2_NET_BUF_REGISTERED) {
				c2_net_buffer_deregister(nb, &ctx->pc_dom);
				c2_bufvec_free(&nb->nb_buffer);
			}
		}
		c2_free(ctx->pc_nbs);
	}
	if (ctx->pc_dom.nd_xprt != NULL)
		c2_net_domain_fini(&ctx->pc_dom);
}

void server(struct ping_ctx *ctx)
{
	int rc;

	rc = ping_init("localhost", SERVER_PORT, ctx);
	C2_ASSERT(rc == 0);

	/*
	  TODO: Insert actual code to do something here.
	 */
	struct c2_time delay, rem;
	c2_time_set(&delay, 2, 0);
	c2_nanosleep(&delay, &rem);

	ping_fini(ctx);
}

void client(struct ping_ctx *ctx)
{
	int rc;

	rc = ping_init("localhost", CLIENT_PORT, ctx);
	C2_ASSERT(rc == 0);

	/*
	  TODO: Insert actual code to do something here.
	 */
	struct c2_time delay, rem;
	c2_time_set(&delay, 2, 0);
	c2_nanosleep(&delay, &rem);

	ping_fini(ctx);
}

int main(int argc, char *argv[])
{
	int                   rc;
	bool		      interact = false;
	bool		      verbose = false;
	const char           *xprt_name = c2_net_bulk_mem_xprt.nx_name;
	int		      loops = DEF_LOOPS;
	int                   nr_bufs = DEF_BUFS;

	struct c2_net_xprt   *xprt;
	struct c2_thread      server_thread;

	rc = c2_init();
	C2_ASSERT(rc == 0);

	rc = C2_GETOPTS("ping", argc, argv,
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
	sctx.pc_xprt = xprt;
	sctx.pc_nr_bufs = nr_bufs;
	C2_SET0(&server_thread);
	rc = C2_THREAD_INIT(&server_thread, struct ping_ctx *, NULL,
			    &server, &sctx);
	C2_ASSERT(rc == 0);
	cctx.pc_xprt = xprt;
	cctx.pc_nr_bufs = nr_bufs;
	client(&cctx);

	/* TODO: really figure out how to tell server thread to stop */
	c2_atomic64_set(&sctx.pc_stop, 1);

	c2_thread_join(&server_thread);

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
