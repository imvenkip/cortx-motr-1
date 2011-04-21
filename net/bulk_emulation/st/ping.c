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

#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "net/net.h"
#include "net/bulk_mem.h"
#include "net/bulk_sunrpc.h"
#include "net/bulk_emulation/st/ping.h"

enum {
	SEND_RETRIES = 3,

	CLIENT_PORT = 31416,
	SERVER_PORT = 27183
};

struct ping_work_item {
	enum c2_net_queue_type      pwi_type;
	struct c2_net_buffer       *pwi_nb;
	struct c2_list_link         pwi_link;
};

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
		if (!c2_bitmap_get(&ctx->pc_nbbm, i) &&
		    !(ctx->pc_nbs[i].nb_flags & C2_NET_BUF_IN_CALLBACK)) {
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
int encode_desc(struct c2_net_buffer *nb,
		bool send_desc,
		const struct c2_net_buf_desc *desc)
{
	struct c2_vec_cursor cur;
	char *bp;
	c2_bcount_t step;

	c2_vec_cursor_init(&cur, &nb->nb_buffer.ov_vec);
	bp = nb->nb_buffer.ov_buf[cur.vc_seg];
	if (send_desc)
		*bp++ = 's';
	else
		*bp++ = 'r';
	C2_ASSERT(!c2_vec_cursor_move(&cur, 1));

	step = c2_vec_cursor_step(&cur);
	C2_ASSERT(step >= 9 + desc->nbd_len);
	nb->nb_length = 10 + desc->nbd_len;

	bp += sprintf(bp, "%08d", desc->nbd_len);
	++bp;				/* +nul */
	memcpy(bp, desc->nbd_data, desc->nbd_len);
	return 0;
}

enum ping_msg_type {
	/** client wants to do passive send, server will active recv */
	PM_SEND_DESC,
	/** client wants to do active send, server will passive recv */
	PM_RECV_DESC,
	PM_MSG
};

struct ping_msg {
	enum ping_msg_type pm_type;
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
	C2_ASSERT(*bp == 'm' || *bp == 's' || *bp == 'r');
	if (*bp == 'm') {
		size_t len = nb->nb_length - 1;
		char *str;

		++bp;
		msg->pm_type = PM_MSG;
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
		if (*bp == 's')
			msg->pm_type = PM_SEND_DESC;
		else
			msg->pm_type = PM_RECV_DESC;
		++bp;
		step = c2_vec_cursor_step(&cur);
		C2_ASSERT(step >= 9 && bp[8] == 0);
		C2_ASSERT(sscanf(bp, "%d", &len) == 1);
		msg->pm_u.pm_desc.nbd_len = len;
		C2_ASSERT(step >= 9 + msg->pm_u.pm_desc.nbd_len);
		bp += 9;
		msg->pm_u.pm_desc.nbd_data = c2_alloc(len);
		memcpy(msg->pm_u.pm_desc.nbd_data, bp, len);
	}
	return 0;
}

void msg_free(struct ping_msg *msg)
{
	if (msg->pm_type != PM_MSG)
		c2_net_desc_free(&msg->pm_u.pm_desc);
	else
		c2_free(msg->pm_u.pm_str);
}

/* client callbacks */
void c_m_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{

	struct ping_ctx *ctx = container_of(tm, struct ping_ctx, pc_tm);
	int rc;
	struct ping_work_item *wi;
	struct ping_msg msg;

	C2_ASSERT(ev->nev_qtype == C2_NET_QT_MSG_RECV);
	ctx->pc_ops->pf("Client: Msg Recv CB\n");

	if (ev->nev_status < 0) {
		if (ev->nev_status == -ECANCELED)
			ctx->pc_ops->pf("Client: msg recv canceled\n");
		else
			ctx->pc_ops->pf("Client: msg recv error: %d\n",
					ev->nev_status);
	} else {
		rc = decode_msg(ev->nev_buffer, &msg);
		C2_ASSERT(rc == 0);

		if (msg.pm_type != PM_MSG) {
			C2_IMPOSSIBLE("Client: got desc\n");
			/* TODO: implement this branch? */
		} else
			ctx->pc_ops->pf("Client: got msg: %s\n",
					msg.pm_u.pm_str);
		msg_free(&msg);
	}

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
	ctx->pc_ops->pf("Client: Msg Send CB\n");

	if (ev->nev_status < 0) {
		if (ev->nev_status == -ECANCELED)
			ctx->pc_ops->pf("Client: msg send canceled\n");
		else
			ctx->pc_ops->pf("Client: msg send error: %d\n",
					ev->nev_status);

		/* let main app deal with it */
		struct ping_work_item *wi;
		C2_ALLOC_PTR(wi);
		c2_list_link_init(&wi->pwi_link);
		wi->pwi_type = C2_NET_QT_MSG_SEND;
		wi->pwi_nb = ev->nev_buffer;

		c2_mutex_lock(&ctx->pc_mutex);
		c2_list_add(&ctx->pc_work_queue, &wi->pwi_link);
		c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
		c2_mutex_unlock(&ctx->pc_mutex);
	} else {
		c2_net_desc_free(&ev->nev_buffer->nb_desc);
		ping_buf_put(ctx, ev->nev_buffer);
	}
}

void c_p_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	struct ping_ctx *ctx = container_of(tm, struct ping_ctx, pc_tm);
	int rc;
	struct ping_work_item *wi;
	struct ping_msg msg;

	C2_ASSERT(ev->nev_qtype == C2_NET_QT_PASSIVE_BULK_RECV);
	ctx->pc_ops->pf("Client: Passive Recv CB\n");

	if (ev->nev_status < 0) {
		if (ev->nev_status == -ECANCELED)
			ctx->pc_ops->pf("Client: passive recv canceled\n");
		else
			ctx->pc_ops->pf("Client: passive recv error: %d\n",
					ev->nev_status);
	} else {
		rc = decode_msg(ev->nev_buffer, &msg);
		C2_ASSERT(rc == 0);

		if (msg.pm_type != PM_MSG)
			C2_IMPOSSIBLE("Client: got desc\n");
		else
			ctx->pc_ops->pf("Client: got data: %s\n",
					msg.pm_u.pm_str);
		msg_free(&msg);
	}

	c2_net_desc_free(&ev->nev_buffer->nb_desc);
	ping_buf_put(ctx, ev->nev_buffer);

	C2_ALLOC_PTR(wi);
	c2_list_link_init(&wi->pwi_link);
	wi->pwi_type = C2_NET_QT_PASSIVE_BULK_RECV;

	c2_mutex_lock(&ctx->pc_mutex);
	c2_list_add(&ctx->pc_work_queue, &wi->pwi_link);
	c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
	c2_mutex_unlock(&ctx->pc_mutex);
}

void c_p_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	struct ping_ctx *ctx = container_of(tm, struct ping_ctx, pc_tm);
	struct ping_work_item *wi;

	C2_ASSERT(ev->nev_qtype == C2_NET_QT_PASSIVE_BULK_SEND);
	ctx->pc_ops->pf("Client: Passive Send CB\n");

	if (ev->nev_status < 0) {
		if (ev->nev_status == -ECANCELED)
			ctx->pc_ops->pf("Client: passive send canceled\n");
		else
			ctx->pc_ops->pf("Client: passive send error: %d\n",
					ev->nev_status);
	}

	c2_net_desc_free(&ev->nev_buffer->nb_desc);
	ping_buf_put(ctx, ev->nev_buffer);

	C2_ALLOC_PTR(wi);
	c2_list_link_init(&wi->pwi_link);
	wi->pwi_type = C2_NET_QT_PASSIVE_BULK_SEND;

	c2_mutex_lock(&ctx->pc_mutex);
	c2_list_add(&ctx->pc_work_queue, &wi->pwi_link);
	c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
	c2_mutex_unlock(&ctx->pc_mutex);
}

void c_a_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	C2_ASSERT(ev->nev_qtype == C2_NET_QT_ACTIVE_BULK_RECV);
	C2_IMPOSSIBLE("Client: Active Recv CB\n");
}

void c_a_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	C2_ASSERT(ev->nev_qtype == C2_NET_QT_ACTIVE_BULK_SEND);
	C2_IMPOSSIBLE("Client: Active Send CB\n");
}

void c_event_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	struct ping_ctx *ctx = container_of(tm, struct ping_ctx, pc_tm);

	C2_ASSERT(ev->nev_qtype == C2_NET_QT_NR);
	if (ev->nev_status == 0) {
		const char *s = "unexpected";
		if (ev->nev_payload == (void *) C2_NET_TM_STARTED)
			s = "started";
		else if (ev->nev_payload == (void *) C2_NET_TM_STOPPED)
			s = "stopped";
		ctx->pc_ops->pf("Client: Event CB state change to %s\n", s);
	} else if (ev->nev_status < 0)
		ctx->pc_ops->pf("Client: Event CB for error %d\n",
				ev->nev_status);
	else
		ctx->pc_ops->pf("Client: Event CB for diagnostic %d\n",
				ev->nev_status);
}

bool server_stop = false;

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
	struct ping_ctx *ctx = container_of(tm, struct ping_ctx, pc_tm);
	int rc;
	struct ping_work_item *wi;
	struct ping_msg msg;

	C2_ASSERT(ev->nev_qtype == C2_NET_QT_MSG_RECV);
	ctx->pc_ops->pf("Server: Msg Recv CB\n");

	if (ev->nev_status < 0) {
		if (ev->nev_status == -ECANCELED && server_stop)
			ctx->pc_ops->pf("Server: "
					"msg recv canceled on shutdown\n");
		else {
			ctx->pc_ops->pf("Service: msg recv error: %d\n",
					ev->nev_status);
			ev->nev_buffer->nb_timeout = C2_TIME_NEVER;
			ev->nev_buffer->nb_ep = NULL;
			rc = c2_net_buffer_add(ev->nev_buffer, &ctx->pc_tm);
			C2_ASSERT(rc == 0);
		}
	} else {
		rc = decode_msg(ev->nev_buffer, &msg);
		C2_ASSERT(rc == 0);

		struct c2_net_buffer *nb = ping_buf_get(ctx);
		if (nb == NULL)
			ctx->pc_ops->pf("Server: dropped msg, "
					"no buffer available\n");
		else {
			C2_ALLOC_PTR(wi);
			nb->nb_ep = ev->nev_buffer->nb_ep; /* save for later */
			ev->nev_buffer->nb_ep = NULL;
			c2_list_link_init(&wi->pwi_link);
			wi->pwi_nb = nb;
			if (msg.pm_type == PM_SEND_DESC) {
				ctx->pc_ops->pf("Server: "
						"got desc for active recv\n");
				wi->pwi_type = C2_NET_QT_ACTIVE_BULK_RECV;
				nb->nb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
				c2_net_desc_copy(&msg.pm_u.pm_desc,
						 &nb->nb_desc);
				nb->nb_ep = NULL; /* not needed */
				C2_ASSERT(rc == 0);
			} else if (msg.pm_type == PM_RECV_DESC) {
				ctx->pc_ops->pf("Server: "
						"got desc for active send\n");
				wi->pwi_type = C2_NET_QT_ACTIVE_BULK_SEND;
				nb->nb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
				c2_net_desc_copy(&msg.pm_u.pm_desc,
						 &nb->nb_desc);
				nb->nb_ep = NULL; /* not needed */
				/* reuse encode_msg for convenience */
				rc = encode_msg(nb, "active pong");
				C2_ASSERT(rc == 0);
			} else {
				ctx->pc_ops->pf("Server: got msg: %s\n",
						msg.pm_u.pm_str);

				/* queue wi to send back ping response */
				rc = c2_net_end_point_get(nb->nb_ep);
				C2_ASSERT(rc == 0);
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
		ev->nev_buffer->nb_timeout = C2_TIME_NEVER;
		ev->nev_buffer->nb_ep = NULL;
		rc = c2_net_buffer_add(ev->nev_buffer, &ctx->pc_tm);
		C2_ASSERT(rc == 0);

		msg_free(&msg);
	}
}

void s_m_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	struct ping_ctx *ctx = container_of(tm, struct ping_ctx, pc_tm);
	int rc;

	C2_ASSERT(ev->nev_qtype == C2_NET_QT_MSG_SEND);
	ctx->pc_ops->pf("Server: Msg Send CB\n");

	if (ev->nev_status < 0) {
		/* no retries here */
		if (ev->nev_status == -ECANCELED)
			ctx->pc_ops->pf("Server: msg send canceled\n");
		else
			ctx->pc_ops->pf("Server: msg send error: %d\n",
					ev->nev_status);
	}

	rc = c2_net_end_point_put(ev->nev_buffer->nb_ep);
	C2_ASSERT(rc == 0);
	ev->nev_buffer->nb_ep = NULL;

	ping_buf_put(ctx, ev->nev_buffer);
}

void s_p_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	C2_ASSERT(ev->nev_qtype == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_IMPOSSIBLE("Server: Passive Recv CB\n");
}

void s_p_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	C2_ASSERT(ev->nev_qtype == C2_NET_QT_PASSIVE_BULK_SEND);
	C2_IMPOSSIBLE("Server: Passive Send CB\n");
}

void s_a_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	struct ping_ctx *ctx = container_of(tm, struct ping_ctx, pc_tm);
	int rc;
	struct ping_msg msg;

	C2_ASSERT(ev->nev_qtype == C2_NET_QT_ACTIVE_BULK_RECV);
	ctx->pc_ops->pf("Server: Active Recv CB\n");

	if (ev->nev_status < 0) {
		/* no retries here */
		if (ev->nev_status == -ECANCELED)
			ctx->pc_ops->pf("Server: active send canceled\n");
		else
			ctx->pc_ops->pf("Server: active send error: %d\n",
					ev->nev_status);
	} else {
		rc = decode_msg(ev->nev_buffer, &msg);
		C2_ASSERT(rc == 0);

		if (msg.pm_type != PM_MSG)
			C2_IMPOSSIBLE("Server: got desc\n");
		else
			ctx->pc_ops->pf("Server: got data: %s\n",
					msg.pm_u.pm_str);
		msg_free(&msg);
	}

	c2_net_desc_free(&ev->nev_buffer->nb_desc);
	ping_buf_put(ctx, ev->nev_buffer);
}

void s_a_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	struct ping_ctx *ctx = container_of(tm, struct ping_ctx, pc_tm);

	C2_ASSERT(ev->nev_qtype == C2_NET_QT_ACTIVE_BULK_SEND);
	ctx->pc_ops->pf("Server: Active Send CB\n");

	if (ev->nev_status < 0) {
		/* no retries here */
		if (ev->nev_status == -ECANCELED)
			ctx->pc_ops->pf("Server: active send canceled\n");
		else
			ctx->pc_ops->pf("Server: active send error: %d\n",
					ev->nev_status);
	}

	c2_net_desc_free(&ev->nev_buffer->nb_desc);
	ping_buf_put(ctx, ev->nev_buffer);
}

void s_event_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	struct ping_ctx *ctx = container_of(tm, struct ping_ctx, pc_tm);

	C2_ASSERT(ev->nev_qtype == C2_NET_QT_NR);
	if (ev->nev_status == 0) {
		const char *s = "unexpected";
		if (ev->nev_payload == (void *) C2_NET_TM_STARTED)
			s = "started";
		else if (ev->nev_payload == (void *) C2_NET_TM_STOPPED)
			s = "stopped";
		ctx->pc_ops->pf("Server: Event CB state change to %s\n", s);
	} else if (ev->nev_status < 0)
		ctx->pc_ops->pf("Server: Event CB for error %d\n",
				ev->nev_status);
	else
		ctx->pc_ops->pf("Server: Event CB for diagnostic %d\n",
				ev->nev_status);
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

	rc = alloc_buffers(ctx->pc_nr_bufs, ctx->pc_segments, ctx->pc_seg_size,
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
			c2_net_buffer_deregister(nb, &ctx->pc_dom);
			c2_bufvec_free(&nb->nb_buffer);
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

void ping_server(struct ping_ctx *ctx)
{
	int i;
	int rc;
	struct c2_net_buffer *nb;

	ctx->pc_tm.ntm_callbacks = &stm_cb;
	C2_ASSERT(ctx->pc_nr_bufs >= 20);
	rc = ping_init("localhost", SERVER_PORT, ctx);
	C2_ASSERT(rc == 0);

	c2_mutex_lock(&ctx->pc_mutex);
	for (i = 0; i < 4; ++i) {
		nb = &ctx->pc_nbs[i];
		nb->nb_qtype = C2_NET_QT_MSG_RECV;
		nb->nb_timeout = C2_TIME_NEVER;
		nb->nb_ep = NULL;
		rc = c2_net_buffer_add(nb, &ctx->pc_tm);
		c2_bitmap_set(&ctx->pc_nbbm, i, true);
		C2_ASSERT(rc == 0);
	}

	while (!server_stop) {
		struct c2_list_link *link;
		struct ping_work_item *wi;
		while (!c2_list_is_empty(&ctx->pc_work_queue)) {
			link = c2_list_first(&ctx->pc_work_queue);
			wi = c2_list_entry(link, struct ping_work_item,
					   pwi_link);
			switch (wi->pwi_type) {
			case C2_NET_QT_MSG_SEND:
			case C2_NET_QT_ACTIVE_BULK_SEND:
			case C2_NET_QT_ACTIVE_BULK_RECV:
				wi->pwi_nb->nb_timeout = C2_TIME_NEVER;
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

void ping_server_should_stop(struct ping_ctx *ctx)
{
	c2_mutex_lock(&ctx->pc_mutex);
	server_stop = true;
	c2_cond_signal(&ctx->pc_cond, &ctx->pc_mutex);
	c2_mutex_unlock(&ctx->pc_mutex);
}

/**
   Test an RPC-like exchange, sending data in a message to the server and
   getting back a response.
   @param ctx client context
   @param server_ep endpoint of the server
   @param data data to send, or NULL to send a default "ping"
   @retval 0 successful test
   @retval -errno failed to send to server
 */
int ping_client_msg_send_recv(struct ping_ctx *ctx,
			      struct c2_net_end_point *server_ep,
			      const char *data)
{
	int rc;
	struct c2_net_buffer *nb;

	ctx->pc_ops->pf("Client: starting msg send/recv sequence\n");
	/* queue buffer for response, must do before sending msg */
	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	nb->nb_qtype = C2_NET_QT_MSG_RECV;
	nb->nb_timeout = C2_TIME_NEVER;
	nb->nb_ep = NULL;
	rc = c2_net_buffer_add(nb, &ctx->pc_tm);
	C2_ASSERT(rc == 0);

	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	rc = encode_msg(nb, "ping");
	nb->nb_qtype = C2_NET_QT_MSG_SEND;
	nb->nb_ep = server_ep;
	C2_ASSERT(rc == 0);
	nb->nb_timeout = C2_TIME_NEVER;
	rc = c2_net_buffer_add(nb, &ctx->pc_tm);
	C2_ASSERT(rc == 0);

	struct c2_list_link *link;
	struct ping_work_item *wi;
	bool recv_done = false;
	int retries = SEND_RETRIES;

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
			else if (wi->pwi_type == C2_NET_QT_MSG_SEND) {
				/* implies send error, retry a few times */
				if (retries == 0) {
					ctx->pc_ops->pf("Client: send failed, "
							"no more retries\n");
					c2_mutex_unlock(&ctx->pc_mutex);
					ping_buf_put(ctx, nb);
					return -ETIMEDOUT;
				}
				struct c2_time delay, rem;
				c2_time_set(&delay,
					    SEND_RETRIES + 1 - retries, 0);
				--retries;
				c2_nanosleep(&delay, &rem);
				rc = c2_net_buffer_add(nb, &ctx->pc_tm);
				C2_ASSERT(rc == 0);
			}
			c2_free(wi);
		}
		if (recv_done)
			break;
		c2_cond_wait(&ctx->pc_cond, &ctx->pc_mutex);
	}

	c2_mutex_unlock(&ctx->pc_mutex);
	return rc;
}

int ping_client_passive_recv(struct ping_ctx *ctx,
			     struct c2_net_end_point *server_ep)
{
	int rc;
	struct c2_net_buffer *nb;
	struct c2_net_buf_desc nbd;

	ctx->pc_ops->pf("Client: starting passive recv sequence\n");
	/* queue our passive receive buffer */
	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	nb->nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	nb->nb_ep = server_ep;
	nb->nb_timeout = C2_TIME_NEVER;
	rc = c2_net_buffer_add(nb, &ctx->pc_tm);
	C2_ASSERT(rc == 0);
	rc = c2_net_desc_copy(&nb->nb_desc, &nbd);
	C2_ASSERT(rc == 0);

	/* send descriptor in message to server */
	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	rc = encode_desc(nb, false, &nbd);
	c2_net_desc_free(&nbd);
	nb->nb_qtype = C2_NET_QT_MSG_SEND;
	nb->nb_ep = server_ep;
	C2_ASSERT(rc == 0);
	nb->nb_timeout = C2_TIME_NEVER;
	rc = c2_net_buffer_add(nb, &ctx->pc_tm);
	C2_ASSERT(rc == 0);

	struct c2_list_link *link;
	struct ping_work_item *wi;
	bool recv_done = false;
	int retries = SEND_RETRIES;

	/* wait for receive to complete */
	c2_mutex_lock(&ctx->pc_mutex);
	while (1) {
		while (!c2_list_is_empty(&ctx->pc_work_queue)) {
			link = c2_list_first(&ctx->pc_work_queue);
			wi = c2_list_entry(link, struct ping_work_item,
					   pwi_link);
			c2_list_del(&wi->pwi_link);
			if (wi->pwi_type == C2_NET_QT_PASSIVE_BULK_RECV)
				recv_done = true;
			else if (wi->pwi_type == C2_NET_QT_MSG_SEND) {
				/* implies send error, retry a few times */
				if (retries == 0) {
					ctx->pc_ops->pf("Client: send failed, "
							"no more retries\n");
					c2_net_desc_free(&nb->nb_desc);
					c2_mutex_unlock(&ctx->pc_mutex);
					ping_buf_put(ctx, nb);
					return -ETIMEDOUT;
				}
				struct c2_time delay, rem;
				c2_time_set(&delay,
					    SEND_RETRIES + 1 - retries, 0);
				--retries;
				c2_nanosleep(&delay, &rem);
				rc = c2_net_buffer_add(nb, &ctx->pc_tm);
				C2_ASSERT(rc == 0);
			}
			c2_free(wi);
		}
		if (recv_done)
			break;
		c2_cond_wait(&ctx->pc_cond, &ctx->pc_mutex);
	}

	c2_mutex_unlock(&ctx->pc_mutex);
	return rc;
}

int ping_client_passive_send(struct ping_ctx *ctx,
			     struct c2_net_end_point *server_ep)
{
	int rc;
	struct c2_net_buffer *nb;
	struct c2_net_buf_desc nbd;

	ctx->pc_ops->pf("Client: starting passive send sequence\n");
	/* queue our passive receive buffer */
	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	/* reuse encode_msg for convenience */
	rc = encode_msg(nb, "passive ping");
	nb->nb_qtype = C2_NET_QT_PASSIVE_BULK_SEND;
	nb->nb_ep = server_ep;
	nb->nb_timeout = C2_TIME_NEVER;
	rc = c2_net_buffer_add(nb, &ctx->pc_tm);
	C2_ASSERT(rc == 0);
	rc = c2_net_desc_copy(&nb->nb_desc, &nbd);
	C2_ASSERT(rc == 0);

	/* send descriptor in message to server */
	nb = ping_buf_get(ctx);
	C2_ASSERT(nb != NULL);
	rc = encode_desc(nb, true, &nbd);
	c2_net_desc_free(&nbd);
	nb->nb_qtype = C2_NET_QT_MSG_SEND;
	nb->nb_ep = server_ep;
	C2_ASSERT(rc == 0);
	nb->nb_timeout = C2_TIME_NEVER;
	rc = c2_net_buffer_add(nb, &ctx->pc_tm);
	C2_ASSERT(rc == 0);

	struct c2_list_link *link;
	struct ping_work_item *wi;
	bool send_done = false;
	int retries = SEND_RETRIES;

	/* wait for send to complete */
	c2_mutex_lock(&ctx->pc_mutex);
	while (1) {
		while (!c2_list_is_empty(&ctx->pc_work_queue)) {
			link = c2_list_first(&ctx->pc_work_queue);
			wi = c2_list_entry(link, struct ping_work_item,
					   pwi_link);
			c2_list_del(&wi->pwi_link);
			if (wi->pwi_type == C2_NET_QT_PASSIVE_BULK_SEND)
				send_done = true;
			else if (wi->pwi_type == C2_NET_QT_MSG_SEND) {
				/* implies send error, retry a few times */
				if (retries == 0) {
					ctx->pc_ops->pf("Client: send failed, "
							"no more retries\n");
					c2_net_desc_free(&nb->nb_desc);
					c2_mutex_unlock(&ctx->pc_mutex);
					ping_buf_put(ctx, nb);
					return -ETIMEDOUT;
				}
				struct c2_time delay, rem;
				c2_time_set(&delay,
					    SEND_RETRIES + 1 - retries, 0);
				--retries;
				c2_nanosleep(&delay, &rem);
				rc = c2_net_buffer_add(nb, &ctx->pc_tm);
				C2_ASSERT(rc == 0);
			}
			c2_free(wi);
		}
		if (send_done)
			break;
		c2_cond_wait(&ctx->pc_cond, &ctx->pc_mutex);
	}

	c2_mutex_unlock(&ctx->pc_mutex);
	return rc;
}

int ping_client_init(struct ping_ctx *ctx, struct c2_net_end_point **server_ep)
{
	int rc;
	char hostbuf[16];

	ctx->pc_tm.ntm_callbacks = &ctm_cb;
	rc = ping_init("localhost", CLIENT_PORT, ctx);
	if (rc != 0)
		return rc;

	/* need end point for the server */
	rc = canon_host("localhost", hostbuf, sizeof(hostbuf));
	if (rc != 0)
		return rc;
	rc = c2_net_end_point_create(server_ep, &ctx->pc_dom,
				     hostbuf, SERVER_PORT, 0);
	return rc;
}

int ping_client_fini(struct ping_ctx *ctx, struct c2_net_end_point *server_ep)
{
	int rc = c2_net_end_point_put(server_ep);
	ping_fini(ctx);
	return rc;
}
