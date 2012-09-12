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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 09/03/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* @todo remove */
#ifndef __KERNEL__
#include <stdio.h>		/* printf */
#endif

/* @todo debug only, remove it */
#ifndef __KERNEL__
#define LOGD(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOGD(format, ...) do {} while (0)
#endif

#include "lib/memory.h"		/* C2_ALLOC_PTR */
#include "lib/misc.h"		/* C2_SET0 */
#include "lib/time.h"		/* c2_time_t */
#include "lib/errno.h"		/* ENOMEM */

#include "net/test/network.h"	/* c2_net_test_network_ctx */
#include "net/test/node.h"	/* c2_net_test_node_ctx */

#include "net/test/node_ping.h"


/**
   @defgroup NetTestPingNodeInternals Ping Node
   @ingroup NetTestInternals

   Ping node service c2_net_test_service_ops:
   - ntso_init
     - allocate ping node context;
     - init buffer queue semaphore
   - ntso_fini
     - finalize network context if it was initialized;
     - fini buffer queue semaphore
     - free ping node context.
   - ntso_step
     - check 'stop test' conditions
     - recovery after failure (send, recv, buffer callback etc.)
   - ntso_cmd_handler
     - C2_NET_TEST_CMD_INIT
       - initialize network context.
       - reset statistics
       - send C2_NET_TEST_CMD_INIT_DONE reply
     - C2_NET_TEST_CMD_START
       - add all buffers to the network queue;
       - send C2_NET_TEST_CMD_START_DONE reply
     - C2_NET_TEST_CMD_STOP
       - remove all buffers from the network queue
       - wait for all buffer callbacks
       - send C2_NET_TEST_CMD_STOP_DONE reply
     - C2_NET_TEST_CMD_STATUS
       - fill and send C2_NET_TEST_CMD_STATUS_DATA reply

   @todo nb_max_receive_msgs > 1 is not supported.

   @{
 */

enum {
	/** Maximum number of retries. @todo send from test console */
	NODE_PING_RETRY_MAX = 3,
};

struct client_buf_status_recv {
	/**
	 * Buffer event for recv buffer.
	 * Set if c2_net_buffer_event.nbe_status != 0.
	 */
	struct c2_net_buffer_event bsr_ev;
	/* Last buffer enqueue result. If != 0 then bsr_ev makes no sense */
	int			   bsr_errno;
};

/** Message descriptor. Every send buffer have one. */
struct client_msg_descr {
	/** Sequence number for the current message */
	uint64_t		   cmd_seq;
	/** Pointer to recv buffer status */
	struct buf_status_recv	  *cmd_recv_status;
	/** Atomic number of callbacks executed */
	struct c2_atomic64	   cmd_cb_index;
	/** Buffer event for send buffer */
	struct c2_net_buffer_event cmd_ev;
	/** Errno from last buffer enqueue operation */
	int			   cmd_errno;
};

struct server_msg_descr {
	/** Errno from last buffer enqueue operation for recv queue */
	int		   smd_errno_recv;
	/** Errno from last buffer enqueue operation for send queue */
	int		   smb_errno_send;
};

/**
 * Test client context.
 * Buffers assignment:
 * - first half of buffers is for sending and second half is for receiving;
 * - recv buffers can receive message from any server;
 * - send buffers will be used for sending to assigned servers. There is
 *   M = c2_net_test_cmd_init.ntci_concurrency buffers for every server
 *   (first M buffers is for server with index 0, then M buffers for
 *   server with index 1 etc.).
 */
struct node_ping_client_ctx {
	/** Number of added to send queue messages */
	struct c2_atomic64	       npcc_msg_sent;
	/**
	 * Number of test messages sent to test server and received back
	 * (including failed) for the test client.
	 */
	struct c2_atomic64	       npcc_msg_rt;
	/**
	 * Number of received test messages:
	 * - from unknown sources;
	 * - contains invalid data;
	 * - contains obsolete data (incorrece seq number etc.)
	 */
	struct c2_atomic64	       npcc_msg_bad;
	/**
	 * Maximum number of sent to test server and received back
	 * test messages (for the test client). "rt" == "round-trip".
	 */
	size_t			       npcc_msg_rt_max;
	/** Array of test client message descriptors */
	struct client_msg_descr	      *npcc_descr;
	/** Array of test client receive buffer status */
	struct client_buf_status_recv *npcc_recv_bs;
	/** Concurrency. @see c2_net_test_cmd_init.ntci_concurrency */
	size_t			       npcc_concurrency;
};

/** Test server context */
struct node_ping_server_ctx {
	/** Array of test server message descriptors */
	struct server_msg_descr *npsc_descr;
};

/** Ping node context */
struct node_ping_ctx {
	/** Network context for testing */
	struct c2_net_test_network_ctx	    npc_net;
	/** Test service. Used when changing service state. */
	struct c2_net_test_service	   *npc_svc;
	/** Node role */
	enum c2_net_test_role		    npc_node_role;
	/**
	   Number of network buffers to send/receive test messages.
	   @see c2_net_test_cmd_init.ntci_concurrency
	 */
	size_t				    npc_buf_nr;
	/** Size of network buffers. */
	c2_bcount_t			    npc_buf_size;
	/** Timeout for test message sending */
	c2_time_t			    npc_buf_send_timeout;
	/** Network-context-was-initialized flag */
	bool				    npc_net_initialized;
	/** Test needs to be stopped */
	bool				    npc_test_stop;
	/** All needed statistics */
	struct c2_net_test_cmd_status_data  npc_status_data;
	/** @todo use spinlock instead of mutex */
	struct c2_mutex			    npc_status_data_lock;
	/**
	 * Buffer enqueue semaphore.
	 * - initial value - number of buffers;
	 * - up() in network buffer callback;
	 * - (down() * number_of_buffers) in node_ping_cmd_stop();
	 * - down() after succesful addition to network buffer queue;
	 * - trydown() before addition to queue. if failed -
	 *   then don't add to queue;
	 * - up() after unsuccesful addition to queue.
	 * @todo problem with semaphore max value can be here
	 */
	struct c2_semaphore		    npc_buf_q_sem;
	union {
		struct node_ping_client_ctx npc__client;
		struct node_ping_server_ctx npc__server;
	};
	/**
	 * Individial client context. Set to NULL on the test server,
	 * set to &node_ping_ctx.npc__client on the test client.
	 */
	struct node_ping_client_ctx	   *npc_client;
	/**
	 * Individial server context. See npc_client.
	 */
	struct node_ping_server_ctx	   *npc_server;
};

static void node_ping_tm_event_cb(const struct c2_net_tm_event *ev)
{
	/* nothing for now */
}

static const struct c2_net_tm_callbacks node_ping_tm_cb = {
	.ntc_event_cb = node_ping_tm_event_cb
};

static struct node_ping_ctx *
node_ping_ctx_from_net_ctx(struct c2_net_test_network_ctx *net_ctx)
{
	return (struct node_ping_ctx *)
	       ((char *) net_ctx - offsetof(struct node_ping_ctx, npc_net));
}

static void node_ping_timestamp_put(struct c2_net_test_network_ctx *net_ctx,
				    uint32_t buf_index,
				    uint64_t seq)
{
	struct c2_net_test_timestamp ts;
	struct c2_net_buffer	    *buf;

	buf = c2_net_test_network_buf(net_ctx, C2_NET_TEST_BUF_PING, buf_index);
	c2_net_test_timestamp_init(&ts, seq);
	/* buffer size should be enough to hold timestamp */
	c2_net_test_timestamp_serialize(C2_NET_TEST_SERIALIZE, &ts,
					&buf->nb_buffer, 0);
}

static bool node_ping_enqueue_pre(struct node_ping_ctx *ctx)
{
	/* check 'stop test' flag */
	if (ctx->npc_test_stop)
		return false;
	/* if semaphore can't be down then testing is stoppind or stopped */
	if (!c2_semaphore_trydown(&ctx->npc_buf_q_sem))
		return false;
	return true;
}

static void node_ping_enqueue_post(struct node_ping_ctx *ctx, int rc)
{
	if (rc != 0)
		c2_semaphore_up(&ctx->npc_buf_q_sem);
}

static int node_ping_msg_recv(struct node_ping_ctx *ctx,
			      size_t buf_index)
{
	size_t index;
	int    rc;

	C2_PRE(ctx != NULL);
	C2_PRE(buf_index < ctx->npc_buf_nr);
	C2_PRE(ergo(ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT,
		    buf_index >= ctx->npc_buf_nr / 2));

	if (!node_ping_enqueue_pre(ctx))
		return -ECANCELED;
	rc = c2_net_test_network_msg_recv(&ctx->npc_net, buf_index);
	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT) {
		index = buf_index - ctx->npc_buf_nr / 2;
		ctx->npc_client->npcc_recv_bs[index].bsr_errno = rc;
	} else {
		ctx->npc_server->npsc_descr[buf_index].smd_errno_recv = rc;
	}
	node_ping_enqueue_post(ctx, rc);
	return rc;
}

static int node_ping_client_send(struct node_ping_ctx *ctx,
				 size_t buf_index)
{
	struct node_ping_client_ctx *cctx;
	struct client_msg_descr	    *descr;
	uint64_t		     seq;
	size_t			     ep_index;
	int			     rc;

	C2_PRE(ctx != NULL && ctx->npc_client != NULL);
	C2_PRE(buf_index < ctx->npc_buf_nr / 2);

	cctx	 = ctx->npc_client;
	ep_index = buf_index / cctx->npcc_concurrency;
	descr	 = &cctx->npcc_descr[buf_index];
	if (!node_ping_enqueue_pre(ctx))
		return -ECANCELED;
	/* check for max number of messages */
	seq = c2_atomic64_add_return(&cctx->npcc_msg_sent, 1);
	/* double parentheses to prevent gcc warning */
	if ((ctx->npc_test_stop = seq > cctx->npcc_msg_rt_max)) {
		c2_atomic64_sub(&cctx->npcc_msg_sent, 1);
		rc = -ECANCELED;
		goto post;
	}
	/* reset message descriptor */
	C2_SET0(descr);
	/* put timestamp and sequence number */
	node_ping_timestamp_put(&ctx->npc_net, buf_index, seq);
	descr->cmd_seq = seq;
	/* add message to send queue */
	rc = descr->cmd_errno =
	     c2_net_test_network_msg_send(&ctx->npc_net, buf_index, ep_index);
post:
	node_ping_enqueue_post(ctx, rc);
	return rc;
}

static ssize_t node_ping_client_search_seq(struct node_ping_client_ctx *cctx,
					   size_t server_index,
					   uint64_t seq)
{
	size_t i;
	size_t index;

	for (i = 0; i < cctx->npcc_concurrency; ++i) {
		index = i + cctx->npcc_concurrency * server_index;
		if (cctx->npcc_descr[index].cmd_seq == seq)
			return i;
	}
	return -1;
}

static int node_ping_server_send(struct node_ping_ctx *ctx,
				 size_t buf_index,
				 struct c2_net_end_point *ep)
{
	return -ENOSYS;
}

static void node_ping_try_enqueue(struct node_ping_ctx *ctx,
				  struct c2_net_test_network_ctx *net_ctx,
				  bool inc_total,
				  uint32_t buf_index,
				  enum c2_net_queue_type q,
				  struct c2_net_end_point *ep,
				  uint32_t ep_index,
				  struct buf_state *bs)
{
	int rc;

	C2_PRE(ctx != NULL);
	C2_PRE(q == C2_NET_QT_MSG_SEND || q == C2_NET_QT_MSG_RECV);

	LOGD("%s, role = %d, buf_index = %u, q = %d, ",
	     __FUNCTION__, ctx->npc_node_role, buf_index, q);
	if (q == C2_NET_QT_MSG_SEND) {
	if (ep != NULL)
			LOGD("ep->nep_addr = %s", ep->nep_addr);
		else
			LOGD("net_ctx->ntc_ep[ep_index]->nep_addr = %s",
			     net_ctx->ntc_ep[ep_index]->nep_addr);
	} else {
		LOGD("RECV!");
	}
	LOGD(", tm ep = %s", net_ctx->ntc_tm->ntm_ep->nep_addr);
	LOGD("\n");

	/* check 'stop test' flag */
	if (ctx->npc_test_stop)
		return;
	/* put timestamp on test client into outgoing messages */
	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT &&
	    q == C2_NET_QT_MSG_SEND) {
		inc_total ? node_ping_msg_sent_up(ctx) : (void) 0;
		node_ping_timestamp_put(net_ctx, buf_index);
	}
	/* check 'stop test' flag */
	if (ctx->npc_test_stop)
		return;
	/* if semaphore can't be down then testing is stoppind or stopped */
	if (!c2_semaphore_trydown(&ctx->npc_buf_q_sem))
		return;
	/* send/recv test message */
	bs->bs_qtype = q;
	LOGD("enqueue, buf length = %lu!\n",
	     c2_net_test_network_buf(net_ctx, C2_NET_TEST_BUF_PING,
				     buf_index)->nb_length);
	rc = q == C2_NET_QT_MSG_SEND ?
		  ep == NULL ?
		  c2_net_test_network_msg_send(net_ctx, buf_index, ep_index) :
		  c2_net_test_network_msg_send_ep(net_ctx, buf_index, ep) :
		  c2_net_test_network_msg_recv(net_ctx, buf_index);
	/* put to retry ringbuf on error */
	if ((bs->bs_errno = rc) != 0) {
		LOGD("enqueue FAILED\n");
		c2_semaphore_up(&ctx->npc_buf_q_sem);
		node_ping_retry_put(ctx, buf_index);
	}
}

static void node_ping_cb_client(struct node_ping_ctx *ctx,
				uint32_t buf_index,
				enum c2_net_queue_type q,
				const struct c2_net_buffer_event *ev)
{
	struct node_ping_client_ctx  *cctx = ctx->npc_client;
	struct c2_net_test_timestamp  ts;
	size_t			      half_buf = ctx->npc_buf_nr / 2;
	ssize_t			      descr_index;
	ssize_t			      server_index;
	c2_time_t		      now = c2_time_now();
	struct client_msg_descr	     *descr = NULL;

	/* save buffer event */
	if (q == C2_NET_QT_MSG_SEND) {
		descr = &cctx->npcc_descr[buf_index];
		descr->cmd_ev = *ev;
	} else {
		cctx->npcc_recv_bs[buf_index - half_buf].bsr_ev = *ev;
		/* message recv failed - add to recv queue again */
		if (ev->nbe_status != 0) {
			node_ping_msg_recv(ctx, buf_index);
			return;
		}
	}
	/* message was succesfully received */
	if (q == C2_NET_QT_MSG_RECV && ev->nbe_status == 0) {
		/* check buffer length and offset */
		if (ev->nbe_length != ctx->npc_buf_size ||
		    ev->nbe_offset != 0)
			goto bad_buf;
		/* search for test server index */
		server_index = c2_net_test_network_ep_search(&ctx->npc_net,
						ev->nbe_ep->nep_addr);
		if (server_index == -1)
			goto bad_buf;
		/* decode buffer */
		if (!node_ping_timestamp_get(&ctx->npc_net, buf_index, &ts))
			goto bad_buf;
		/* search sequence number */
		descr_index = node_ping_client_search_seq(ctx->npc_client,
							  server_index,
							  ts.ntt_seq);
		if (descr_index == -1)
			goto bad_buf;
		descr = &cctx->npcc_descr[descr_index];
		/* check time in received buffer */
		if (!c2_time_after(now, ts.ntt_time))
			goto bad_buf;
		/* update RTT statistics */
		c2_mutex_lock(&ctx->npc_status_data_lock);
		c2_net_test_stats_time_add(&ctx->npc_status_data.ntcsd_rtt,
					   c2_time_sub(now, ts.ntt_time));
		c2_mutex_unlock(&ctx->npc_status_data_lock);
		goto good_buf;
bad_buf:
		c2_atomic64_inc(&cctx->npcc_msg_bad);
		node_ping_msg_recv(ctx, buf_index);
		return;
good_buf:
		;
	}
	C2_ASSERT(descr != NULL);
	/*
	 * handle message in the second executed callback (it will have
	 * all information about message sending and receiving).
	 */
	if (c2_atomic64_add_return(&descr->cmd_cb_index, 1) == 1)
		return;
	/* write memory barrier should be here */
}

static void node_ping_cb_server(struct node_ping_ctx *ctx,
				uint32_t buf_index,
				enum c2_net_queue_type q,
				const struct c2_net_buffer_event *ev)
{
	/* if message was succesfully received then send it back */
	if (ev->nbe_status == 0 && q == C2_NET_QT_MSG_RECV &&
	    node_ping_server_send(ctx, buf_index, ev->nbe_ep) == 0)
		return;
	/*
	 * - message recv failed OR
	 * - message was sent (succesfully or not) OR
	 * - addition to message send queue failed.
	 */
	node_ping_msg_recv(ctx, buf_index);
}

static void node_ping_msg_cb(struct c2_net_test_network_ctx *net_ctx,
			     uint32_t buf_index,
			     enum c2_net_queue_type q,
			     const struct c2_net_buffer_event *ev)
{
	struct c2_net_test_msg_nr *msg_nr;
	struct node_ping_ctx	  *ctx;

	C2_PRE(q == C2_NET_QT_MSG_RECV || q == C2_NET_QT_MSG_SEND);

	ctx = node_ping_ctx_from_net_ctx(net_ctx);

	LOGD("%s,      role = %d, buf_index = %u, nbe_status = %d, q = %d",
	     __FUNCTION__, ctx->npc_node_role, buf_index, ev->nbe_status, q);
	LOGD(", ev->nbe_length = %lu", ev->nbe_length);

	if (q == C2_NET_QT_MSG_RECV && ev->nbe_status == 0)
		LOGD(", ev->nbe_ep->nep_addr = %s", ev->nbe_ep->nep_addr);

	LOGD("\n");

	c2_semaphore_up(&ctx->npc_buf_q_sem);

	/* test is stopping */
	if (ev->nbe_status == -ECANCELED)
		return;

	/* update messages statistics */
	msg_nr = q == C2_NET_QT_MSG_SEND ?
		 &ctx->npc_status_data.ntcsd_msg_nr_send :
		 &ctx->npc_status_data.ntcsd_msg_nr_recv;

	c2_mutex_lock(&ctx->npc_status_data_lock);
	++msg_nr->ntmn_total;
	msg_nr->ntmn_failed += ev->nbe_status != 0;
	c2_mutex_unlock(&ctx->npc_status_data_lock);

	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT)
		node_ping_cb_client(ctx, buf_index, q, ev);
	else
		node_ping_cb_server(ctx, buf_index, q, ev);
}

static void node_ping_cb_impossible(struct c2_net_test_network_ctx *ctx,
				    const uint32_t buf_index,
				    enum c2_net_queue_type q,
				    const struct c2_net_buffer_event *ev)
{
	C2_IMPOSSIBLE("Impossible network bulk callback: "
		      "net-test ping node can't have it.");
}

static struct c2_net_test_network_buffer_callbacks node_ping_buf_cb = {
	.ntnbc_cb = {
		[C2_NET_QT_MSG_RECV]		= node_ping_msg_cb,
		[C2_NET_QT_MSG_SEND]		= node_ping_msg_cb,
		[C2_NET_QT_PASSIVE_BULK_RECV]	= node_ping_cb_impossible,
		[C2_NET_QT_PASSIVE_BULK_SEND]	= node_ping_cb_impossible,
		[C2_NET_QT_ACTIVE_BULK_RECV]	= node_ping_cb_impossible,
		[C2_NET_QT_ACTIVE_BULK_SEND]	= node_ping_cb_impossible,
	}
};

static int node_ping_client_start(struct node_ping_ctx *ctx)
{
	struct node_ping_client_ctx *cctx;
	size_t			     i;
	size_t			     half_buf;
	int			     rc;
	size_t			     recv_failed = 0;
	size_t			     send_failed = 0;

	C2_PRE(ctx != NULL && ctx->npc_client != NULL);

	cctx	 = ctx->npc_client;
	half_buf = ctx->npc_buf_nr / 2;
	/* enqueue recv buffers */
	for (i = 0; i < half_buf; ++i) {
		rc = node_ping_msg_recv(ctx, i + half_buf);
		recv_failed += (cctx->npcc_recv_bs[i].bsr_errno = rc) != 0;
	}
	/*
	 * If no buffers in recv queue, then return error code from
	 * the first recv buffer - most probably other buffers have
	 * the same error code.
	 */
	if (recv_failed == half_buf)
		return cctx->npcc_recv_bs[0].bsr_errno;
	/* enqueue send buffers */
	for (i = 0; i < half_buf; ++i)
		send_failed += node_ping_client_send(ctx, i) != 0;
	/* the same as for recv queue */
	if (send_failed == half_buf) {
		/* dequeue all recv buffers */
		for (i = 0; i < half_buf; ++i)
			c2_net_test_network_buffer_dequeue(&ctx->npc_net,
					C2_NET_TEST_TYPE_PING, i + half_buf);
		/* wait for buffer callbacks */
		for (i = 0; i < half_buf; ++i)
			c2_semaphore_down(&ctx->npc_buf_q_sem);
		return cctx->npcc_descr[0].cmd_errno;
	}
	return 0;
}

static int node_ping_server_start(struct node_ping_ctx *ctx)
{
	struct node_ping_server_ctx *sctx;
	size_t			     i;
	size_t			     recv_failed = 0;

	C2_PRE(ctx != NULL && ctx->npc_server != NULL);

	sctx = ctx->npc_server;
	for (i = 0; i < ctx->npc_buf_nr; ++i)
		recv_failed += node_ping_msg_recv(ctx, i) != 0;
	/* @see node_ping_client_start() */
	return recv_failed < ctx->npc_buf_nr ?
	       0 : sctx->npsc_descr[0].smd_errno_recv;
}

static void *node_ping_init_fini(void *ctx_,
				 struct c2_net_test_service *svc,
				 bool init)
{
	struct node_ping_ctx *ctx = ctx_;

	C2_PRE(equi(init, ctx == NULL));
	C2_PRE(equi(init, svc != NULL));

	if (init) {
		C2_ALLOC_PTR(ctx);
		if (ctx != NULL) {
			ctx->npc_svc = svc;
			c2_mutex_init(&ctx->npc_status_data_lock);
		}
	} else {
		if (ctx->npc_net_initialized) {
			c2_net_test_network_ctx_fini(&ctx->npc_net);
		}
		c2_mutex_fini(&ctx->npc_status_data_lock);
		c2_free(ctx);
	}
	return init ? ctx : NULL;
}

static void *node_ping_init(struct c2_net_test_service *svc)
{
	return node_ping_init_fini(NULL, svc, true);
}

static void node_ping_fini(void *ctx_)
{
	void *rc = node_ping_init_fini(ctx_, NULL, false);
	C2_POST(rc == NULL);
}

static int node_ping_step(void *ctx_)
{
	struct c2_net_test_cmd_status_data *sd;
	struct c2_net_test_msg_nr	    msg_send;
	struct c2_net_test_msg_nr	    msg_recv;
	struct node_ping_ctx		   *ctx = ctx_;
	c2_time_t			    now;

	C2_PRE(ctx != NULL);
	sd = &ctx->npc_status_data;

	/* update MPS stats */
	c2_mutex_lock(&ctx->npc_status_data_lock);
	msg_send = sd->ntcsd_msg_nr_send;
	msg_recv = sd->ntcsd_msg_nr_recv;
	c2_mutex_unlock(&ctx->npc_status_data_lock);

	now = c2_time_now();
	c2_net_test_mps_add(&sd->ntcsd_mps_send, msg_send.ntmn_total, now);
	c2_net_test_mps_add(&sd->ntcsd_mps_recv, msg_recv.ntmn_total, now);

	return 0;
}

static int node_ping_cmd_init(void *ctx_,
			      const struct c2_net_test_cmd *cmd,
			      struct c2_net_test_cmd *reply)
{
	struct c2_net_test_network_timeouts  timeouts;
	struct node_ping_ctx		    *ctx = ctx_;
	int				     rc;
	int				     i;

	C2_PRE(ctx != NULL);
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);

	LOGD("%s\n", __FUNCTION__);

	/* ep wasn't recognized */
	if (cmd->ntc_ep_index == -1) {
		rc = -ENOENT;
		goto reply;
	}
	/* network context already initialized */
	if (ctx->npc_net_initialized) {
		rc = -EALREADY;
		goto reply;
	}
	/* check command type */
	C2_ASSERT(cmd->ntc_init.ntci_type == C2_NET_TEST_TYPE_PING);
	/* parse INIT command */
	ctx->npc_node_role	  = cmd->ntc_init.ntci_role;
	ctx->npc_buf_size	  = cmd->ntc_init.ntci_msg_size;
	ctx->npc_buf_send_timeout = cmd->ntc_init.ntci_buf_send_timeout;

	ctx->npc_buf_nr	 = cmd->ntc_init.ntci_concurrency;
	ctx->npc_buf_nr *= ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT ?
			   2 * cmd->ntc_init.ntci_ep.ntsl_nr : 1;

	ctx->npc_client = ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT ?
			  &ctx->npc__client : NULL;
	ctx->npc_server = ctx->npc_node_role == C2_NET_TEST_ROLE_SERVER ?
			  &ctx->npc__server : NULL;

	/* do sanity check */
	rc = -EINVAL;
	if (ctx->npc_buf_size < 1 || ctx->npc_buf_nr < 1)
		goto reply;
	if (equi(ctx->npc_client == NULL, ctx->npc_server == NULL))
		goto reply;

	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT) {
		c2_atomic64_set(&ctx->npc_client->npcc_msg_sent, 0);
		c2_atomic64_set(&ctx->npc_client->npcc_msg_rt, 0);
		ctx->npc_client->npcc_msg_rt_max = cmd->ntc_init.ntci_msg_nr;
		ctx->npc_client->npcc_concurrency =
			cmd->ntc_init.ntci_concurrency;

		rc = -ENOMEM;
		C2_ALLOC_ARR(ctx->npc_client->npcc_descr, ctx->npc_buf_nr / 2);
		if (ctx->npc_client->npcc_descr == NULL)
			goto reply;
		C2_ALLOC_ARR(ctx->npc_client->npcc_recv_bs,
			     ctx->npc_buf_nr / 2);
		if (ctx->npc_client->npcc_recv_bs == NULL) {
			c2_free(ctx->npc_client->npcc_descr);
			goto reply;
		}
	} else if (ctx->npc_node_role == C2_NET_TEST_ROLE_SERVER) {
		C2_ALLOC_ARR(ctx->npc_server->npsc_descr, ctx->npc_buf_nr);
		if (ctx->npc_server->npsc_descr == NULL) {
			rc = -ENOMEM;
			goto reply;
		}
	} else {
		rc = -EINVAL;
		goto reply;
	}

	rc = c2_semaphore_init(&ctx->npc_buf_q_sem, ctx->npc_buf_nr);
	if (rc != 0)
		goto reply;

	/* initialize network context */
	timeouts = c2_net_test_network_timeouts_never();
	timeouts.ntnt_timeout[C2_NET_QT_MSG_SEND] = ctx->npc_buf_send_timeout;
	rc = c2_net_test_network_ctx_init(&ctx->npc_net,
					  cmd->ntc_init.ntci_tm_ep,
					  &node_ping_tm_cb,
					  &node_ping_buf_cb,
					  ctx->npc_buf_size,
					  ctx->npc_buf_nr,
					  0, 0,
					  cmd->ntc_init.ntci_ep.ntsl_nr,
					  &timeouts);
	if (rc != 0)
		goto free_buf_q_sem;
	/* add test node endpoints to the network context endpoint list */
	for (i = 0; i < cmd->ntc_init.ntci_ep.ntsl_nr; ++i) {
		if ((rc = c2_net_test_network_ep_add(&ctx->npc_net,
				cmd->ntc_init.ntci_ep.ntsl_list[i])) < 0)
			goto free_net_ctx;
	}
	ctx->npc_net_initialized = true;
	rc = 0;
	goto reply;
free_net_ctx:
	c2_net_test_network_ctx_fini(&ctx->npc_net);
free_buf_q_sem:
	c2_semaphore_fini(&ctx->npc_buf_q_sem);
reply:
	if (rc != 0) {
		/* change service state */
		c2_net_test_service_state_change(ctx->npc_svc,
						 C2_NET_TEST_SERVICE_FAILED);
	}
	/* fill reply */
	reply->ntc_type = C2_NET_TEST_CMD_INIT_DONE;
	reply->ntc_done.ntcd_errno = rc;
	return rc;
}

static int node_ping_cmd_start(void *ctx_,
			       const struct c2_net_test_cmd *cmd,
			       struct c2_net_test_cmd *reply)
{
	struct c2_net_test_cmd_status_data *sd;
	struct node_ping_ctx		   *ctx = ctx_;
	int				    rc;
	c2_time_t			    _1s;

	C2_PRE(ctx != NULL);
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);

	LOGD("%s\n", __FUNCTION__);

	sd = &ctx->npc_status_data;
	C2_SET0(sd);

	/* fill test start time */
	sd->ntcsd_time_start = c2_time_now();
	/* initialize stats */
	c2_time_set(&_1s, 1, 0);
	c2_net_test_mps_init(&sd->ntcsd_mps_send, 0, sd->ntcsd_time_start, _1s);
	c2_net_test_mps_init(&sd->ntcsd_mps_recv, 0, sd->ntcsd_time_start, _1s);
	c2_net_test_stats_reset(&sd->ntcsd_rtt);
	/* start test */
	rc = ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT ?
	     node_ping_client_start(ctx) : node_ping_server_start(ctx);
	/* fill reply */
	reply->ntc_type = C2_NET_TEST_CMD_START_DONE;
	reply->ntc_done.ntcd_errno = rc;
	return rc;
}

static int node_ping_cmd_stop(void *ctx_,
			      const struct c2_net_test_cmd *cmd,
			      struct c2_net_test_cmd *reply)
{
	struct node_ping_ctx *ctx = ctx_;
	int		      i;

	C2_PRE(ctx != NULL);
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);

	LOGD("%s\n", __FUNCTION__);

	/* dequeue all buffers */
	ctx->npc_test_stop = true;
	for (i = 0; i < ctx->npc_buf_nr; ++i) {
		c2_net_test_network_buffer_dequeue(&ctx->npc_net,
						   C2_NET_TEST_BUF_PING, i);
	}
	/* wait for buffer callbacks */
	for (i = 0; i < ctx->npc_buf_nr; ++i)
		c2_semaphore_down(&ctx->npc_buf_q_sem);
	/* change service state */
	c2_net_test_service_state_change(ctx->npc_svc,
					 C2_NET_TEST_SERVICE_FINISHED);
	/* fill reply */
	reply->ntc_type = C2_NET_TEST_CMD_STOP_DONE;
	reply->ntc_done.ntcd_errno = 0;
	return 0;
}

static int node_ping_cmd_status(void *ctx_,
				const struct c2_net_test_cmd *cmd,
				struct c2_net_test_cmd *reply)
{
	struct c2_net_test_cmd_status_data *sd;
	struct node_ping_ctx		   *ctx = ctx_;
	size_t				    nr;

	C2_PRE(ctx != NULL);
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);

	sd  = &reply->ntc_status_data;

	reply->ntc_type = C2_NET_TEST_CMD_STATUS_DATA;

	c2_mutex_lock(&ctx->npc_status_data_lock);
	*sd = ctx->npc_status_data;
	c2_mutex_unlock(&ctx->npc_status_data_lock);

	sd->ntcsd_time_now = c2_time_now();

	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT) {
		nr = c2_atomic64_get(&ctx->npc_client->npcc_msg_rt);
		sd->ntcsd_finished = nr >= ctx->npc_client->npcc_msg_rt_max;
	}

	return 0;
}

static struct c2_net_test_service_cmd_handler node_ping_cmd_handler[] = {
	{
		.ntsch_type    = C2_NET_TEST_CMD_INIT,
		.ntsch_handler = node_ping_cmd_init,
	},
	{
		.ntsch_type    = C2_NET_TEST_CMD_START,
		.ntsch_handler = node_ping_cmd_start,
	},
	{
		.ntsch_type    = C2_NET_TEST_CMD_STOP,
		.ntsch_handler = node_ping_cmd_stop,
	},
	{
		.ntsch_type    = C2_NET_TEST_CMD_STATUS,
		.ntsch_handler = node_ping_cmd_status,
	},
};

struct c2_net_test_service_ops c2_net_test_node_ping_ops = {
	.ntso_init	     = node_ping_init,
	.ntso_fini	     = node_ping_fini,
	.ntso_step	     = node_ping_step,
	.ntso_cmd_handler    = node_ping_cmd_handler,
	.ntso_cmd_handler_nr = ARRAY_SIZE(node_ping_cmd_handler),
};

/**
   @} end of NetTestPingNodeInternals group
 */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
