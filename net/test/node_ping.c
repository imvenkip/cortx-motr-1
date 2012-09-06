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
       - add all buffers to the recv queue;
       - send C2_NET_TEST_CMD_START_DONE reply
     - C2_NET_TEST_CMD_STOP
       - remove all buffers from the recv queue
       - wait for all buffer callbacks
       - send C2_NET_TEST_CMD_STOP_DONE reply
     - C2_NET_TEST_CMD_STATUS
       - fill and send C2_NET_TEST_CMD_STATUS_DATA reply

   Ping node service c2_net_test_network_buffer_callbacks:
   - C2_NET_QT_MSG_RECV
     - update buffer status
     - update stats
     - check 'stop test' conditions
     - add message to the queue:
       - test client: C2_NET_QT_MSG_RECV queue
       - test server: C2_NET_QT_MSG_SEND queue
   - C2_NET_QT_MSG_SEND
     - update buffer status
     - update stats
     - check 'stop test' conditions
     - add message to the queue:
       - test server: C2_NET_QT_MSG_RECV queue
       - test client: C2_NET_QT_MSG_SEND queue
   - C2_NET_QT_PASSIVE_BULK_RECV, C2_NET_QT_ACTIVE_BULK_RECV,
     C2_NET_QT_PASSIVE_BULK_SEND, C2_NET_QT_ACTIVE_BULK_SEND
     - C2_IMPOSSIBLE("...")

   Error conditions
   - Buffer can't be added to C2_NET_QT_MSG_RECV queue;
   - Buffer can't be added to C2_NET_QT_MSG_SEND queue;
   - Buffer callback with -ECANCELED (send queue)
   - Buffer callback with -ECANCELED (recv queue)
   - Buffer callback with -ETIMEDOUT (send queue)
   - Buffer callback with nbe_status != 0 (send queue)
   - Buffer callback with nbe_status != 0 (recv queue)

   @todo Error handling. Now after first error buffer will be disabled.
   @todo nb_max_receive_msgs > 1 is not supported.

   @{
 */

enum {
	/** Maximum number of retries. @todo send from test console */
	NODE_PING_RETRY_MAX = 3,
};

struct buf_state {
	/**
	 * Error code for the last enqueue operation with buffer.
	 * Set after adding buffer to queue.
	 */
	int			 bs_errno;
	/** Buffer status. Set in buffer callback. */
	int			 bs_status;
	/** Buffer queue type. Set before adding buffer to queue. */
	enum c2_net_queue_type	 bs_qtype;
	/** Number of retries. */
	size_t			 bs_retry_nr;
	/** Last retry time. */
	c2_time_t		 bs_retry_time;
	/**
	 * Endpoint for test server.
	 * c2_net_end_point_get() (c2_net_end_point_put()) in
	 * C2_NET_QT_MSG_RECV (C2_NET_QT_MSG_SEND) callback on the
	 * test server iff c2_net_buffer_event.nbe_status == 0.
	 * c2_net_end_point_put() in node_ping_retry_all_q() if
	 * number of retries exceeded maximum.
	 */
	struct c2_net_end_point *bs_ep;
	/** Endpoint index for the test client */
	size_t			 bs_ep_index;
};

/** Ping node context */
struct node_ping_ctx {
	/** Node context */
	struct c2_net_test_node_ctx	  *npc_node_ctx;
	/** Node role */
	enum c2_net_test_role		   npc_node_role;
	/**
	   Number of network buffers to send/receive test messages.
	   @see c2_net_test_cmd_init.ntci_concurrency
	 */
	size_t				   npc_buf_nr;
	/** Size of network buffers. */
	c2_bcount_t			   npc_buf_size;
	/** Timeout for test message sending */
	c2_time_t			   npc_buf_send_timeout;
	/** Number of added to send queue messages on test client */
	struct c2_atomic64		   npc_msg_nr;
	/** Maximum number of test messages (for the test client) */
	size_t				   npc_msg_nr_max;
	/** Network-context-was-initialized flag */
	bool				   npc_net_initialized;
	/** Test needs to be stopped */
	bool				   npc_test_stop;
	/** All needed statistics */
	struct c2_net_test_cmd_status_data npc_status_data;
	/** @todo use spinlock instead of mutex */
	struct c2_mutex			   npc_status_data_lock;
	/** Buffers state */
	struct buf_state		  *npc_buf_state;
	/** Receiving retry ringbuf */
	struct c2_net_test_ringbuf	   npc_retry_rb_recv;
	/** Sending retry ringbuf */
	struct c2_net_test_ringbuf	   npc_retry_rb_send;
	/** Maximum number of retries */
	size_t				   npc_retry_nr_max;
	/**
	 * Buffer enqueue semaphore.
	 * up() in network callback, if addition to queue failed
	 * or not needed;
	 * down() in node_ping_cmd_stop().
	 * @todo problem with semaphore max value can be here
	 */
	struct c2_semaphore		   npc_buf_q_sem;
};

static struct node_ping_ctx
*node_ping_ctx_from_net_ctx(struct c2_net_test_network_ctx *net_ctx)
{
	return c2_net_test_node_ctx_from_net_ctx(net_ctx)->ntnc_svc_private;
}

static struct node_ping_ctx
*node_ping_ctx_from_node_ctx(struct c2_net_test_node_ctx *node_ctx)
{
	return node_ctx->ntnc_svc_private;
}

static void node_ping_tm_event_cb(const struct c2_net_tm_event *ev)
{
	/* nothing for now */
}

static const struct c2_net_tm_callbacks node_ping_tm_cb = {
	.ntc_event_cb = node_ping_tm_event_cb
};

static void node_ping_timestamp_put(struct c2_net_test_network_ctx *net_ctx,
				    uint32_t buf_index)
{
	struct c2_net_test_timestamp ts;
	struct c2_net_buffer	    *buf;

	buf = c2_net_test_network_buf(net_ctx, C2_NET_TEST_BUF_PING, buf_index);
	c2_net_test_timestamp_init(&ts);
	/* buffer size should be enough to hold timestamp */
	c2_net_test_timestamp_serialize(C2_NET_TEST_SERIALIZE, &ts,
					&buf->nb_buffer, 0);
}

static c2_time_t
node_ping_timestamp_get(struct c2_net_test_network_ctx *net_ctx,
			uint32_t buf_index)
{
	struct c2_net_test_timestamp ts;
	struct c2_net_buffer	    *buf;
	c2_bcount_t		     rc_bcount;

	buf = c2_net_test_network_buf(net_ctx, C2_NET_TEST_BUF_PING, buf_index);
	rc_bcount = c2_net_test_timestamp_serialize(C2_NET_TEST_SERIALIZE, &ts,
						    &buf->nb_buffer, 0);
	return rc_bcount == 0 ? C2_TIME_NEVER : c2_net_test_timestamp_get(&ts);
}

static void node_ping_retry_put(struct node_ping_ctx *ctx,
				uint32_t buf_index)
{
	struct c2_net_test_ringbuf *rb;

	rb = ctx->npc_buf_state->bs_qtype == C2_NET_QT_MSG_SEND ?
	     &ctx->npc_retry_rb_send : &ctx->npc_retry_rb_recv;
	c2_net_test_ringbuf_push(rb, buf_index);
}

static ssize_t node_ping_retry_get(struct node_ping_ctx *ctx,
				   enum c2_net_queue_type q)
{
	struct c2_net_test_ringbuf *rb;

	rb = q == C2_NET_QT_MSG_SEND ? &ctx->npc_retry_rb_send :
				       &ctx->npc_retry_rb_recv;
	return c2_net_test_ringbuf_is_empty(rb) ? -1 :
						  c2_net_test_ringbuf_pop(rb);
}

static void node_ping_rtt_update(struct node_ping_ctx *ctx,
				 struct c2_net_test_network_ctx *net_ctx,
				 uint32_t buf_index)
{
	c2_time_t timestamp;
	c2_time_t now;
	c2_time_t rtt;

	now = c2_time_now();
	timestamp = node_ping_timestamp_get(net_ctx, buf_index);
	if (!c2_time_after(now, timestamp))
		c2_time_set(&rtt, 0, 0);
	else
		rtt = c2_time_sub(c2_time_now(), timestamp);

	c2_mutex_lock(&ctx->npc_status_data_lock);
	c2_net_test_stats_time_add(&ctx->npc_status_data.ntcsd_rtt, rtt);
	c2_mutex_unlock(&ctx->npc_status_data_lock);
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
	size_t total;
	int    rc;
	bool   stop;

	C2_PRE(ctx != NULL);
	C2_PRE(q == C2_NET_QT_MSG_SEND || q == C2_NET_QT_MSG_RECV);

	LOGD("%s, role = %d, buf_index = %u, q = %d\n",
	     __FUNCTION__, ctx->npc_node_role, buf_index, q);

	/* put timestamp on test client into outgoing messages */
	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT &&
	    q == C2_NET_QT_MSG_SEND) {
		if (inc_total) {
			total = c2_atomic64_add_return(&ctx->npc_msg_nr, 1);
			stop = total > ctx->npc_msg_nr_max;
			if (stop && !ctx->npc_test_stop) {
				ctx->npc_status_data.ntcsd_time_finish =
					c2_time_now();
			}
			ctx->npc_test_stop = stop;
		}
		node_ping_timestamp_put(net_ctx, buf_index);
	}
	/* check 'stop test' flag */
	if (ctx->npc_test_stop)
		return;
	/* send/recv test message */
	bs->bs_qtype = q;
	rc = q == C2_NET_QT_MSG_SEND ?
		  ep == NULL ?
		  c2_net_test_network_msg_send(net_ctx, buf_index, ep_index) :
		  c2_net_test_network_msg_send_ep(net_ctx, buf_index, ep) :
		  c2_net_test_network_msg_recv(net_ctx, buf_index);
	/* put to retry ringbuf on error */
	if ((bs->bs_errno = rc) != 0)
		node_ping_retry_put(ctx, buf_index);
}

static void node_ping_retry_all_q(struct node_ping_ctx *ctx,
				  struct c2_net_test_network_ctx *net_ctx,
				  const enum c2_net_queue_type q)
{
	struct c2_net_test_msg_nr *msg_nr;
	struct buf_state	  *bs;
	ssize_t			   buf_index;
	ssize_t			   buf_index_first;
	enum c2_net_queue_type	   enqueue_q;

	C2_PRE(ctx != NULL);

	msg_nr = q == C2_NET_QT_MSG_SEND ?
		 &ctx->npc_status_data.ntcsd_msg_nr_send :
		 &ctx->npc_status_data.ntcsd_msg_nr_recv;
	buf_index_first = buf_index = node_ping_retry_get(ctx, q);
	while (buf_index != -1 && buf_index != buf_index_first) {
		bs = &ctx->npc_buf_state[buf_index];
		C2_ASSERT(q == bs->bs_qtype);

		enqueue_q = q;
		/*
		 * If retry number exceeded max value,
		 * then msg send/recv failed.
		 */
		if (bs->bs_retry_nr > ctx->npc_retry_nr_max) {
			bs->bs_retry_nr = 0;

			c2_mutex_lock(&ctx->npc_status_data_lock);
			++msg_nr->ntmn_fails;
			c2_mutex_unlock(&ctx->npc_status_data_lock);

			/*
			 * c2_net_end_point_put() if it was
			 * c2_net_end_point_get().
			 */
			if (ctx->npc_node_role == C2_NET_TEST_ROLE_SERVER &&
			    q == C2_NET_QT_MSG_SEND) {
				c2_net_end_point_put(bs->bs_ep);
				bs->bs_ep = NULL;
				enqueue_q = C2_NET_QT_MSG_RECV;
			}
		}
		/* increase retry number for buffer */
		++bs->bs_retry_nr;

		c2_mutex_lock(&ctx->npc_status_data_lock);
		++msg_nr->ntmn_retries;
		c2_mutex_unlock(&ctx->npc_status_data_lock);

		node_ping_try_enqueue(ctx, net_ctx, bs->bs_retry_nr == 1,
				      buf_index, enqueue_q, bs->bs_ep,
				      bs->bs_ep_index, bs);

		buf_index = node_ping_retry_get(ctx, q);
	}
}

static void node_ping_msg_cb(struct c2_net_test_network_ctx *net_ctx,
			     uint32_t buf_index,
			     enum c2_net_queue_type q,
			     const struct c2_net_buffer_event *ev)
{
	struct node_ping_ctx	  *ctx;
	struct c2_net_test_msg_nr *msg_nr;
	struct buf_state	  *bs;

	C2_PRE(q == C2_NET_QT_MSG_RECV || q == C2_NET_QT_MSG_SEND);

	ctx = node_ping_ctx_from_net_ctx(net_ctx);
	bs = &ctx->npc_buf_state[buf_index];

	LOGD("%s, role = %d, nbe_status = %d\n",
	     __FUNCTION__, ctx->npc_node_role, ev->nbe_status);

	if ((bs->bs_status = ev->nbe_status) != 0) {
		/* retry if failed */
		node_ping_retry_put(ctx, buf_index);
		return;
	}

	/* update success msg nr */
	msg_nr = q == C2_NET_QT_MSG_SEND ?
		 &ctx->npc_status_data.ntcsd_msg_nr_send :
		 &ctx->npc_status_data.ntcsd_msg_nr_recv;

	c2_mutex_lock(&ctx->npc_status_data_lock);
	++msg_nr->ntmn_total;
	c2_mutex_unlock(&ctx->npc_status_data_lock);

	/* update RTT if received test message on test client */
	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT &&
	    q == C2_NET_QT_MSG_RECV)
		node_ping_rtt_update(ctx, net_ctx, buf_index);
	/* save/release buffer_state endpoint on the test server */
	if (ctx->npc_node_role == C2_NET_TEST_ROLE_SERVER) {
		if (q == C2_NET_QT_MSG_RECV) {
			c2_net_end_point_get(ev->nbe_ep);
			bs->bs_ep = ev->nbe_ep;
		} else {
			c2_net_end_point_put(bs->bs_ep);
		}
	}
	/* revert queue on test server */
	if (ctx->npc_node_role == C2_NET_TEST_ROLE_SERVER) {
		q = q == C2_NET_QT_MSG_RECV ? C2_NET_QT_MSG_SEND :
					      C2_NET_QT_MSG_RECV;
	}
	node_ping_try_enqueue(ctx, net_ctx, true, buf_index, q,
			      bs->bs_ep, 0, bs);
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

static int node_ping_init_fini(struct c2_net_test_node_ctx *node_ctx, bool init)
{
	struct node_ping_ctx *ctx;
	int		      rc = 0;

	C2_PRE(node_ctx != NULL);

	if (!init)
		goto fini;

	rc = -ENOMEM;
	C2_ALLOC_PTR(ctx);
	if (ctx == NULL)
		goto failed;

	node_ctx->ntnc_svc_private = ctx;
	ctx->npc_node_ctx	   = node_ctx;

	rc = c2_semaphore_init(&ctx->npc_buf_q_sem, 0);
	if (rc != 0)
		goto free_ping_ctx;

	c2_mutex_init(&ctx->npc_status_data_lock);
	goto success;

fini:
	ctx = node_ctx->ntnc_svc_private;
	if (ctx->npc_net_initialized)
		c2_net_test_network_ctx_fini(&node_ctx->ntnc_net);
	c2_free(ctx->npc_buf_state);
	c2_mutex_fini(&ctx->npc_status_data_lock);
	c2_semaphore_fini(&ctx->npc_buf_q_sem);
free_ping_ctx:
	c2_free(ctx);
failed:
success:
	return rc;
}

static int node_ping_init(struct c2_net_test_node_ctx *node_ctx)
{
	return node_ping_init_fini(node_ctx, true);
}

static void node_ping_fini(struct c2_net_test_node_ctx *node_ctx)
{
	int rc = node_ping_init_fini(node_ctx, false);
	C2_POST(rc == 0);
}

static int node_ping_step(struct c2_net_test_node_ctx *node_ctx)
{
	struct c2_net_test_cmd_status_data *sd;
	struct c2_net_test_msg_nr	    msg_send;
	struct c2_net_test_msg_nr	    msg_recv;
	struct node_ping_ctx		   *ctx;
	c2_time_t			    now;

	C2_PRE(node_ctx != NULL);
	ctx = node_ping_ctx_from_node_ctx(node_ctx);
	sd = &ctx->npc_status_data;

	/* update MPS stats */
	c2_mutex_lock(&ctx->npc_status_data_lock);
	msg_send = sd->ntcsd_msg_nr_send;
	msg_recv = sd->ntcsd_msg_nr_recv;
	c2_mutex_unlock(&ctx->npc_status_data_lock);

	now = c2_time_now();
	c2_net_test_mps_add(&sd->ntcsd_mps_send, msg_send.ntmn_total, now);
	c2_net_test_mps_add(&sd->ntcsd_mps_recv, msg_recv.ntmn_total, now);

	/* try to enqueue again all failed buffers */
	node_ping_retry_all_q(ctx, &node_ctx->ntnc_net, C2_NET_QT_MSG_SEND);
	node_ping_retry_all_q(ctx, &node_ctx->ntnc_net, C2_NET_QT_MSG_RECV);
	return 0;
}

static int node_ping_cmd_init(struct c2_net_test_node_ctx *node_ctx,
			      const struct c2_net_test_cmd *cmd,
			      struct c2_net_test_cmd *reply)
{
	struct c2_net_test_network_timeouts timeouts;
	struct node_ping_ctx		   *ctx;
	int				    rc;
	int				    i;

	C2_PRE(node_ctx != NULL);
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);

	LOGD(__FUNCTION__);

	/* ep wasn't recognized */
	if (cmd->ntc_ep_index == -1) {
		rc = -ENOENT;
		goto reply;
	}
	/* network context already initialized */
	ctx = node_ping_ctx_from_node_ctx(node_ctx);
	if (ctx->npc_net_initialized) {
		rc = -EINVAL;
		goto reply;
	}
	/* check command type */
	C2_ASSERT(cmd->ntc_init.ntci_type == C2_NET_TEST_TYPE_PING);
	/* parse INIT command */
	ctx->npc_node_role	  = cmd->ntc_init.ntci_role;
	ctx->npc_buf_size	  = cmd->ntc_init.ntci_msg_size;
	ctx->npc_buf_send_timeout = cmd->ntc_init.ntci_buf_send_timeout;
	ctx->npc_msg_nr_max	  = cmd->ntc_init.ntci_msg_nr;
	ctx->npc_retry_nr_max	  = NODE_PING_RETRY_MAX;
	c2_atomic64_set(&ctx->npc_msg_nr, 0);

	ctx->npc_buf_nr	 = cmd->ntc_init.ntci_concurrency;
	ctx->npc_buf_nr *= ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT ?
			   2 * cmd->ntc_init.ntci_ep.ntsl_nr : 1;

	C2_ALLOC_ARR(ctx->npc_buf_state, ctx->npc_buf_nr);
	if (ctx->npc_buf_state == NULL)
		goto reply;

	rc = c2_net_test_ringbuf_init(&ctx->npc_retry_rb_recv, ctx->npc_buf_nr);
	if (rc != 0)
		goto free_buf_state;

	rc = c2_net_test_ringbuf_init(&ctx->npc_retry_rb_send, ctx->npc_buf_nr);
	if (rc != 0)
		goto free_rb_recv;

	/* initialize network context */
	timeouts = c2_net_test_network_timeouts_never();
	timeouts.ntnt_timeout[C2_NET_QT_MSG_SEND] = ctx->npc_buf_send_timeout;
	rc = c2_net_test_network_ctx_init(&node_ctx->ntnc_net,
					  cmd->ntc_init.ntci_tm_ep,
					  &node_ping_tm_cb,
					  &node_ping_buf_cb,
					  ctx->npc_buf_nr,
					  ctx->npc_buf_size,
					  0, 0,
					  cmd->ntc_init.ntci_ep.ntsl_nr,
					  &timeouts);
	if (rc != 0)
		goto free_rb_send;
	/* add test node endpoints to the network context endpoint list */
	for (i = 0; i < cmd->ntc_init.ntci_ep.ntsl_nr; ++i) {
		if ((rc = c2_net_test_network_ep_add(&node_ctx->ntnc_net,
				cmd->ntc_init.ntci_ep.ntsl_list[i])) < 0)
			goto free_net_ctx;
	}
	ctx->npc_net_initialized = true;
	rc = 0;
	goto reply;
free_net_ctx:
	c2_net_test_network_ctx_fini(&node_ctx->ntnc_net);
free_rb_send:
	c2_net_test_ringbuf_fini(&ctx->npc_retry_rb_send);
free_rb_recv:
	c2_net_test_ringbuf_fini(&ctx->npc_retry_rb_recv);
free_buf_state:
	c2_free(ctx->npc_buf_state);
reply:
	/* fill reply */
	reply->ntc_type = C2_NET_TEST_CMD_INIT_DONE;
	reply->ntc_done.ntcd_errno = rc;
	return rc;
}

static int node_ping_cmd_start(struct c2_net_test_node_ctx *node_ctx,
				 const struct c2_net_test_cmd *cmd,
				 struct c2_net_test_cmd *reply)
{
	struct c2_net_test_cmd_status_data *sd;
	struct node_ping_ctx		   *ctx;
	struct buf_state		   *bs;
	enum c2_net_queue_type		    q;
	int				    rc;
	int				    i;
	c2_time_t			    _1s;
	size_t				    bufs_per_server;

	C2_PRE(node_ctx != NULL);
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);

	LOGD(__FUNCTION__);

	ctx = node_ping_ctx_from_node_ctx(node_ctx);
	if (ctx->npc_buf_nr == 0) {
		rc = -EINVAL;
		goto reply;
	}
	sd = &ctx->npc_status_data;
	C2_SET0(sd);

	/* fill test start time */
	sd->ntcsd_time_start = c2_time_now();
	/* initialize stats */
	c2_time_set(&_1s, 1, 0);
	c2_net_test_mps_init(&sd->ntcsd_mps_send, 0, sd->ntcsd_time_start, _1s);
	c2_net_test_mps_init(&sd->ntcsd_mps_recv, 0, sd->ntcsd_time_start, _1s);
	c2_net_test_stats_reset(&sd->ntcsd_rtt);
	/* enqueue all buffers */
	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT)
		bufs_per_server = ctx->npc_buf_nr /
				  node_ctx->ntnc_net.ntc_ep_nr;
	for (i = 0; i < ctx->npc_buf_nr; ++i) {
		bs = &ctx->npc_buf_state[i];
		C2_SET0(bs);
		/* enqueue RECV buffers before SEND buffers on test client */
		q = ctx->npc_node_role == C2_NET_TEST_ROLE_SERVER ||
		    i % 2 == 0 ? C2_NET_QT_MSG_RECV : C2_NET_QT_MSG_SEND;
		if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT)
			bs->bs_ep_index = i / bufs_per_server;
		node_ping_try_enqueue(ctx, &node_ctx->ntnc_net, true, i, q,
				      NULL, bs->bs_ep_index,
				      &ctx->npc_buf_state[i]);
	}
	rc = 0;
reply:
	/* fill reply */
	reply->ntc_type = C2_NET_TEST_CMD_START_DONE;
	reply->ntc_done.ntcd_errno = rc;
	return rc;
}

static int node_ping_cmd_stop(struct c2_net_test_node_ctx *node_ctx,
			      const struct c2_net_test_cmd *cmd,
			      struct c2_net_test_cmd *reply)
{
	struct node_ping_ctx *ctx;
	int		      i;

	C2_PRE(node_ctx != NULL);
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);

	LOGD(__FUNCTION__);

	ctx = node_ping_ctx_from_node_ctx(node_ctx);
	/* dequeue all buffers */
	ctx->npc_test_stop = true;
	for (i = 0; i < ctx->npc_buf_nr; ++i) {
		c2_net_test_network_buffer_dequeue(&node_ctx->ntnc_net,
						   C2_NET_TEST_BUF_PING, i);
	}
	/* wait for buffer callbacks */
	for (i = 0; i < ctx->npc_buf_nr; ++i)
		c2_semaphore_down(&ctx->npc_buf_q_sem);
	/* fill reply */
	reply->ntc_type = C2_NET_TEST_CMD_STOP_DONE;
	reply->ntc_done.ntcd_errno = 0;
	return 0;
}

static int node_ping_cmd_status(struct c2_net_test_node_ctx *node_ctx,
				const struct c2_net_test_cmd *cmd,
				struct c2_net_test_cmd *reply)
{
	struct c2_net_test_cmd_status_data *sd;
	struct node_ping_ctx		   *ctx;

	C2_PRE(node_ctx != NULL);
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);

	LOGD(__FUNCTION__);

	ctx = node_ping_ctx_from_node_ctx(node_ctx);
	sd  = &reply->ntc_status_data;

	reply->ntc_type = C2_NET_TEST_CMD_STATUS_DATA;

	c2_mutex_lock(&ctx->npc_status_data_lock);
	*sd = ctx->npc_status_data;
	c2_mutex_unlock(&ctx->npc_status_data_lock);

	sd->ntcsd_time_now = c2_time_now();

	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT)
		sd->ntcsd_finished = ctx->npc_msg_nr_max ==
				     sd->ntcsd_msg_nr_send.ntmn_total;

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
