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

/**
   Context for a ping client or server.
 */
struct ping_ctx {
	struct c2_net_xprt         *pc_xprt;
	struct c2_net_domain        pc_dom;
	uint32_t                    pc_nr_bufs;
	struct c2_net_buffer       *pc_nbs;
	struct c2_bitmap	    pc_nbbm;
	struct c2_net_end_point    *pc_ep;
	struct c2_net_transfer_mc   pc_tm;
	struct c2_mutex		    pc_mutex;
	struct c2_cond		    pc_cond;
	struct c2_list              pc_work_queue;
};

struct ping_work_item {
	enum c2_net_queue_type      pwi_type;
	struct c2_net_buffer       *pwi_nb;
	struct c2_list_link         pwi_link;
};

struct c2_net_xprt *xprts[3] = {
	&c2_net_bulk_mem_xprt,
	&c2_net_bulk_sunrpc_xprt,
	NULL
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

/**
   Get a unused buffer from the context buffer list.
   On success, marks the buffer as in-use and returns it.
   @retval ptr the buffer
   @retval NULL failure
 */
struct c2_net_buffer *ping_buf_get(struct ping_ctx *ctx)
{
	int i;

	c2_mutex_lock(&ctx->pc_mutex);
	for (i = 0; i < ctx->pc_nr_bufs; ++i)
		if (!c2_bitmap_get(&ctx->pc_nbbm, i)) {
			c2_bitmap_set(&ctx->pc_nbbm, i, true);
			break;
		}
	c2_mutex_unlock(&ctx->pc_mutex);

	if (i == ctx->pc_nr_bufs)
		return NULL;

	struct c2_net_buffer *nb = &ctx->pc_nbs[i];
	C2_ASSERT(nb->nb_flags == C2_NET_BUF_REGISTERED);
	return nb;
}

/**
   Releases a buffer back to the free buffer pool.
   The buffer is marked as not in-use.
 */
void ping_buf_put(struct ping_ctx *ctx, struct c2_net_buffer *nb)
{
	int i = nb - &ctx->pc_nbs[0];
	C2_ASSERT(i >= 0 && i < ctx->pc_nr_bufs);
	C2_ASSERT((nb->nb_flags &
		   ~(C2_NET_BUF_REGISTERED | C2_NET_BUF_IN_CALLBACK)) == 0);

	c2_mutex_lock(&ctx->pc_mutex);
	C2_ASSERT(c2_bitmap_get(&ctx->pc_nbbm, i));
	c2_bitmap_set(&ctx->pc_nbbm, i, false);
	c2_mutex_unlock(&ctx->pc_mutex);
}

/** encode a string message into a net buffer, not zero-copy */
int encode_msg(struct c2_net_buffer *nb, const char *str)
{
	struct c2_vec_cursor cur;
	char *bp;
	size_t len = strlen(str) + 1; /* include trailing nul */
	c2_bcount_t step;

	nb->nb_length = len + 1;

	c2_vec_cursor_init(&cur, &nb->nb_buffer.ov_vec);
	bp = nb->nb_buffer.ov_buf[cur.vc_seg];
	*bp++ = 'm';
	C2_ASSERT(!c2_vec_cursor_move(&cur, 1));
	while (len > 0) {
		step = c2_vec_cursor_step(&cur);
		if (len > step) {
			memcpy(bp, str, step);
			str += step;
			len -= step;
			C2_ASSERT(!c2_vec_cursor_move(&cur, step));
			C2_ASSERT(cur.vc_offset == 0);
			bp = nb->nb_buffer.ov_buf[cur.vc_seg];
		} else {
			memcpy(bp, str, len);
			len = 0;
		}
	}
	return 0;
}

/** encode a descriptor into a net buffer, not zero-copy */
int encode_desc(struct c2_net_buffer *nb, const struct c2_net_buf_desc *desc)
{
	struct c2_vec_cursor cur;
	char *bp;
	c2_bcount_t step;

	c2_vec_cursor_init(&cur, &nb->nb_buffer.ov_vec);
	bp = nb->nb_buffer.ov_buf[cur.vc_seg];
	*bp++ = 'd';
	C2_ASSERT(!c2_vec_cursor_move(&cur, 1));

	step = c2_vec_cursor_step(&cur);
	C2_ASSERT(step >= 9 + desc->nbd_len);
	nb->nb_length = 10 + desc->nbd_len;

	bp += sprintf(bp, "%08d", desc->nbd_len);
	++bp;				/* +nul */
	memcpy(bp, desc->nbd_data, desc->nbd_len);
	return 0;
}

struct ping_msg {
	bool pm_is_desc;
	union {
		char *pm_str;
		struct c2_net_buf_desc pm_desc;
	} pm_u;
};

/** decode a net buffer, allocates memory and copies payload */
int decode_msg(struct c2_net_buffer *nb, struct ping_msg *msg)
{
	struct c2_vec_cursor cur;
	char *bp;
	c2_bcount_t step;

	c2_vec_cursor_init(&cur, &nb->nb_buffer.ov_vec);
	bp = nb->nb_buffer.ov_buf[cur.vc_seg];
	C2_ASSERT(!c2_vec_cursor_move(&cur, 1));
	C2_ASSERT(*bp == 'm' || *bp == 'd');
	if (*bp == 'm') {
		size_t len = nb->nb_length - 1;
		char *str;

		++bp;
		msg->pm_is_desc = false;
		str = msg->pm_u.pm_str = c2_alloc(len);
		while (len > 0) {
			step = c2_vec_cursor_step(&cur);
			if (len > step) {
				memcpy(str, bp, step);
				str += step;
				len -= step;
				C2_ASSERT(!c2_vec_cursor_move(&cur, step));
				C2_ASSERT(cur.vc_offset == 0);
				bp = nb->nb_buffer.ov_buf[cur.vc_seg];
			} else {
				memcpy(str, bp, len);
				len = 0;
			}
		}
	} else {
		int len;
		++bp;
		msg->pm_is_desc = false;
		step = c2_vec_cursor_step(&cur);
		C2_ASSERT(step >= 9 && bp[8] == 0);
		C2_ASSERT(sscanf(bp, "%d", &len) == 1);
		msg->pm_u.pm_desc.nbd_len = len;
		C2_ASSERT(step == 9 + msg->pm_u.pm_desc.nbd_len);
		bp += 9;
		msg->pm_u.pm_desc.nbd_data = c2_alloc(len);
		memcpy(msg->pm_u.pm_desc.nbd_data, bp, len);
	}
	return 0;
}

void msg_free(struct ping_msg *msg)
{
	if (msg->pm_is_desc)
		c2_net_desc_free(&msg->pm_u.pm_desc);
	else
		c2_free(msg->pm_u.pm_str);
}

/* client callbacks */
void c_m_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	C2_ASSERT(ev->nev_qtype == C2_NET_QT_MSG_RECV);
	printf("Client Msg Recv CB: type == %d\n", ev->nev_qtype);
	printf("Client: ep ref cnt = %ld\n",
	       c2_atomic64_get(&ev->nev_buffer->nb_ep->nep_ref.ref_cnt));

	struct ping_ctx *ctx = container_of(tm, struct ping_ctx, pc_tm);
	int rc;
	struct ping_work_item *wi;
	struct ping_msg msg;

	rc = decode_msg(ev->nev_buffer, &msg);
	C2_ASSERT(rc == 0);

	if (msg.pm_is_desc) {
		printf("Client: got desc\n");
		/* TODO: implement this branch? */
		C2_IMPOSSIBLE("Client: desc get not implemented");
	} else
		printf("Client: got msg: %s\n", msg.pm_u.pm_str);
	msg_free(&msg);

	ping_buf_put(ctx, ev->nev_buffer);

	C2_ALLOC_PTR(wi);
	c2_list_link_init(&wi->pwi_link);
	wi->pwi_type = C2_NET_QT_MSG_RECV;

	c2_mutex_lock(&ctx->pc_mutex);
	c2_list_add(&ctx->pc_work_queue, &wi->pwi_link);
	c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
	c2_mutex_unlock(&ctx->pc_mutex);
}

void c_m_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	struct ping_ctx *ctx = container_of(tm, struct ping_ctx, pc_tm);

	C2_ASSERT(ev->nev_qtype == C2_NET_QT_MSG_SEND);
	printf("Client Msg Send CB: type == %d\n", ev->nev_qtype);

	ping_buf_put(ctx, ev->nev_buffer);
}

void c_p_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	printf("Client Passive Recv CB: type == %d\n", ev->nev_qtype);
}

void c_p_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	printf("Client Passive Send CB: type == %d\n", ev->nev_qtype);
}

void c_a_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	printf("Client Active Recv CB: type == %d\n", ev->nev_qtype);
}

void c_a_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	printf("Client Active Send CB: type == %d\n", ev->nev_qtype);
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
	C2_ASSERT(ev->nev_qtype == C2_NET_QT_MSG_RECV);
	printf("Server Msg Recv CB: type == %d, status = %d\n", ev->nev_qtype,
	       ev->nev_status);
	if (ev->nev_buffer->nb_ep != NULL)
		printf("Server: ep ref cnt = %ld\n",
		       c2_atomic64_get(
				      &ev->nev_buffer->nb_ep->nep_ref.ref_cnt));
	else
		printf("Server: ep is NULL\n");

	struct ping_ctx *ctx = container_of(tm, struct ping_ctx, pc_tm);
	int rc;
	struct ping_work_item *wi;
	struct ping_msg msg;

	rc = decode_msg(ev->nev_buffer, &msg);
	C2_ASSERT(rc == 0);

	struct c2_net_buffer *nb = ping_buf_get(ctx);
	if (nb == NULL)
		printf("Server: dropped msg response, no buffer available\n");
	else {
		C2_ALLOC_PTR(wi);
		nb->nb_ep = ev->nev_buffer->nb_ep;
		rc = c2_net_end_point_get(nb->nb_ep);
		C2_ASSERT(rc == 0);
		c2_list_link_init(&wi->pwi_link);
		wi->pwi_nb = nb;
		if (msg.pm_is_desc) {
			printf("Server: got desc\n");
			/* TODO: implement this branch */
			C2_IMPOSSIBLE("Server: desc get not implemented");
		} else {
			printf("Server: got msg: %s\n", msg.pm_u.pm_str);

			/* queue wi to send back ping response */
			wi->pwi_type = C2_NET_QT_MSG_SEND;
			nb->nb_qtype = C2_NET_QT_MSG_SEND;
			rc = encode_msg(nb, "pong");
			C2_ASSERT(rc == 0);
		}
		c2_mutex_lock(&ctx->pc_mutex);
		c2_list_add(&ctx->pc_work_queue, &wi->pwi_link);
		c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
		c2_mutex_unlock(&ctx->pc_mutex);
	}
	rc = c2_net_buffer_add(ev->nev_buffer, &ctx->pc_tm);
	C2_ASSERT(rc == 0);

	msg_free(&msg);
}

void s_m_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{

	C2_ASSERT(ev->nev_qtype == C2_NET_QT_MSG_SEND);
	printf("Server Msg Send CB: type == %d\n", ev->nev_qtype);

	struct ping_ctx *ctx = container_of(tm, struct ping_ctx, pc_tm);
	int rc;

	rc = c2_net_end_point_put(ev->nev_buffer->nb_ep);
	C2_ASSERT(rc == 0);
	ev->nev_buffer->nb_ep = NULL;

	ping_buf_put(ctx, ev->nev_buffer);
}

void s_p_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	printf("Server Passive Recv CB: type == %d\n", ev->nev_qtype);
}

void s_p_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	printf("Server Passive Send CB: type == %d\n", ev->nev_qtype);
}

void s_a_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	printf("Server Active Recv CB: type == %d\n", ev->nev_qtype);
}

void s_a_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	printf("Server Active Send CB: type == %d\n", ev->nev_qtype);
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

bool server_stop = false;
bool server_ready = false;

void ping_fini(struct ping_ctx *ctx);

/**
   Resolve hostname into a dotted quad.  The result is stored in buf.
   @retval 0 success
   @retval -errno failure
 */
int canon_host(const char *hostname, char *buf, size_t bufsiz)
{
	int                i;
	int		   rc = 0;
	struct in_addr     ipaddr;

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
		for (i = 0; hp->h_addr_list[i] != NULL; ++i)
			/* take 1st IPv4 address found */
			if (hp->h_addrtype == AF_INET &&
			    hp->h_length == sizeof(ipaddr))
				break;
		if (hp->h_addr_list[i] == NULL) {
			fprintf(stderr, "No IPv4 address for %s\n",
				hostname);
			return -EPFNOSUPPORT;
		}
		if (inet_ntop(hp->h_addrtype, hp->h_addr, buf, bufsiz) ==
		    NULL) {
			fprintf(stderr, "Cannot parse network address for %s\n",
				hostname);
			rc = -errno;
		}
	} else {
		if (strlen(hostname) >= bufsiz) {
			fprintf(stderr, "Buffer size too small for %s\n",
				hostname);
			return -ENOSPC;
		}
		strcpy(buf, hostname);
	}
	return rc;
}

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
	char               hostbuf[16]; /* big enough for 255.255.255.255 */

	c2_list_init(&ctx->pc_work_queue);

	rc = canon_host(hostname, hostbuf, sizeof(hostbuf));
	if (rc != 0)
		goto fail;

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
	c2_bitmap_init(&ctx->pc_nbbm, ctx->pc_nr_bufs);
	for (i = 0; i < ctx->pc_nr_bufs; ++i) {
		rc = c2_net_buffer_register(&ctx->pc_nbs[i], &ctx->pc_dom);
		if (rc != 0) {
			fprintf(stderr, "buffer register failed: %d\n", rc);
			goto fail;
		}
	}

	rc = c2_net_end_point_create(&ctx->pc_ep, &ctx->pc_dom,
				     hostbuf, port, 0);
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
			C2_ASSERT(nb->nb_flags == C2_NET_BUF_REGISTERED);
			if (nb->nb_flags == C2_NET_BUF_REGISTERED) {
				c2_net_buffer_deregister(nb, &ctx->pc_dom);
				c2_bufvec_free(&nb->nb_buffer);
			}
		}
		c2_free(ctx->pc_nbs);
	}
	if (ctx->pc_dom.nd_xprt != NULL)
		c2_net_domain_fini(&ctx->pc_dom);

	struct c2_list_link *link;
	struct ping_work_item *wi;

	while (!c2_list_is_empty(&ctx->pc_work_queue)) {
		link = c2_list_first(&ctx->pc_work_queue);
		wi = c2_list_entry(link, struct ping_work_item, pwi_link);
		c2_list_del(&wi->pwi_link);
		c2_free(wi);
	}
	c2_list_fini(&ctx->pc_work_queue);
}

void server(struct ping_ctx *ctx)
{
	int i;
	int rc;
	struct c2_net_buffer *nb;

	C2_ASSERT(ctx->pc_nr_bufs >= 20);
	rc = ping_init("localhost", SERVER_PORT, ctx);
	C2_ASSERT(rc == 0);

	c2_mutex_lock(&ctx->pc_mutex);
	for (i = 0; i < 4; ++i) {
		nb = &ctx->pc_nbs[i];
		nb->nb_qtype = C2_NET_QT_MSG_RECV;
		rc = c2_net_buffer_add(nb, &ctx->pc_tm);
		c2_bitmap_set(&ctx->pc_nbbm, i, true);
		C2_ASSERT(rc == 0);
	}
	/* A real client would need to handle timeouts, retransmissions... */
	c2_mutex_lock(&cctx.pc_mutex);
	server_ready = true;
	c2_cond_signal(&cctx.pc_cond, &cctx.pc_mutex);
	c2_mutex_unlock(&cctx.pc_mutex);

	while (!server_stop) {
		struct c2_list_link *link;
		struct ping_work_item *wi;
		while (!c2_list_is_empty(&ctx->pc_work_queue)) {
			link = c2_list_first(&ctx->pc_work_queue);
			wi = c2_list_entry(link, struct ping_work_item,
					   pwi_link);
			switch (wi->pwi_type) {
			case C2_NET_QT_MSG_SEND:
				printf("SERVER: work item\n");
				rc = c2_net_buffer_add(wi->pwi_nb, &ctx->pc_tm);
				C2_ASSERT(rc == 0);
				break;
			default:
				C2_IMPOSSIBLE("unexpected wi->pwi_type");
			}
			c2_list_del(&wi->pwi_link);
			c2_free(wi);
		}
		c2_cond_wait(&ctx->pc_cond, &ctx->pc_mutex);
	}
	c2_mutex_unlock(&ctx->pc_mutex);

	/* dequeue recv buffers */
	struct c2_clink tmwait;
	c2_clink_init(&tmwait, NULL);

	for (i = 0; i < 4; ++i) {
		nb = &ctx->pc_nbs[i];
		c2_clink_add(&ctx->pc_tm.ntm_chan, &tmwait);
		rc = c2_net_buffer_del(nb, &ctx->pc_tm);
		c2_bitmap_set(&ctx->pc_nbbm, i, false);
		C2_ASSERT(rc == 0);
		c2_chan_wait(&tmwait);
		c2_clink_del(&tmwait);
	}

	ping_fini(ctx);
}

void client(struct ping_ctx *ctx)
{
	int rc;
	struct c2_net_buffer *nb;
	struct c2_net_end_point *server_ep;
	char               hostbuf[16];

	rc = ping_init("localhost", CLIENT_PORT, ctx);
	C2_ASSERT(rc == 0);

	/* need end point for the server */
	rc = canon_host("localhost", hostbuf, sizeof(hostbuf));
	C2_ASSERT(rc == 0);
	rc = c2_net_end_point_create(&server_ep, &ctx->pc_dom,
				     hostbuf, SERVER_PORT, 0);
	C2_ASSERT(rc == 0);

	c2_mutex_lock(&ctx->pc_mutex);
	while (!server_ready)
		c2_cond_wait(&ctx->pc_cond, &ctx->pc_mutex);
	c2_mutex_unlock(&ctx->pc_mutex);

#if 1
	nb = NULL;
#else
	/* queue buffer for response, must do before sending msg */
	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	nb->nb_qtype = C2_NET_QT_MSG_RECV;
	rc = c2_net_buffer_add(nb, &ctx->pc_tm);
	C2_ASSERT(rc == 0);

	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	rc = encode_msg(nb, "ping");
	nb->nb_qtype = C2_NET_QT_MSG_SEND;
	nb->nb_ep = server_ep;
	C2_ASSERT(rc == 0);
	rc = c2_net_buffer_add(nb, &ctx->pc_tm);
	C2_ASSERT(rc == 0);

	struct c2_list_link *link;
	struct ping_work_item *wi;
	bool recv_done = false;

	/* wait for receive response to complete */
	c2_mutex_lock(&ctx->pc_mutex);
	while (1) {
		while (!c2_list_is_empty(&ctx->pc_work_queue)) {
			link = c2_list_first(&ctx->pc_work_queue);
			wi = c2_list_entry(link, struct ping_work_item,
					   pwi_link);
			c2_list_del(&wi->pwi_link);
			if (wi->pwi_type == C2_NET_QT_MSG_RECV)
				recv_done = true;
			c2_free(wi);
		}
		if (recv_done)
			break;
		c2_cond_wait(&ctx->pc_cond, &ctx->pc_mutex);
	}
	c2_mutex_unlock(&ctx->pc_mutex);
#endif
	/*
	  TODO: Insert code to do bulk here.
	 */

	printf("Client: ep ref cnt = %ld\n",
	       c2_atomic64_get(&server_ep->nep_ref.ref_cnt));
	/*C2_ASSERT(c2_atomic64_get(&server_ep->nep_ref.ref_cnt) == 1);*/
	c2_net_end_point_put(server_ep);
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
	c2_mutex_init(&sctx.pc_mutex);
	c2_cond_init(&sctx.pc_cond);
	sctx.pc_xprt = xprt;
	sctx.pc_nr_bufs = nr_bufs;
	C2_SET0(&server_thread);
	rc = C2_THREAD_INIT(&server_thread, struct ping_ctx *, NULL,
			    &server, &sctx);
	C2_ASSERT(rc == 0);
	c2_mutex_init(&cctx.pc_mutex);
	c2_cond_init(&cctx.pc_cond);
	cctx.pc_xprt = xprt;
	cctx.pc_nr_bufs = nr_bufs;
	/* client returns when the test is complete */
	client(&cctx);
	c2_cond_fini(&cctx.pc_cond);
	c2_mutex_fini(&cctx.pc_mutex);

	c2_mutex_lock(&sctx.pc_mutex);
	server_stop = true;
	c2_cond_signal(&sctx.pc_cond, &sctx.pc_mutex);
	c2_mutex_unlock(&sctx.pc_mutex);
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
