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

struct buf_state {
	/*
	 * Error code for the last enqueue operation with buffer.
	 * Set after adding buffer to queue.
	 */
	int		       bs_errno;
	/* Buffer status. Set in buffer callback. */
	int		       bs_status;
	/* Buffer queue type. Set before adding buffer to queue. */
	enum c2_net_queue_type bs_qtype;
	/* Number of retries. */
	size_t		       bs_retry_nr;
	/* Last retry time. */
	c2_time_t	       bs_retry_time;
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
	/** Network-context-was-initialized flag */
	bool				   npc_net_initialized;
	/** Test start time */
	c2_time_t			   npc_time_start;
	/** Test needs to be stopped */
	bool				   npc_test_stop;
	/** Messages number statistics */
	struct c2_net_test_msg_nr	   npc_msg_nr;
	/** 'send' bandwidth statistics with 1 sec interval */
	struct c2_net_test_stats_bandwidth npc_bandwidth_1s_send;
	/** 'receive' bandwidth statistics with 1 sec interval */
	struct c2_net_test_stats_bandwidth npc_bandwidth_1s_recv;
	/** RTT statistics */
	struct c2_net_test_stats	   npc_rtt;
	/** @todo use spinlock instead of mutex */
	struct c2_mutex			   npc_rtt_lock;
	/** Buffers state */
	struct buf_state		  *npc_buf_state;
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

static int64_t node_ping_msg_stats_update(struct node_ping_ctx *ctx,
					  const struct buf_state *bs,
					  enum c2_net_queue_type q,
					  const struct c2_net_buffer_event *ev)
{
	struct c2_atomic64 *pa64;
	bool		    success = ev->nbe_status == 0;
	/* update messages number */
	pa64 = q == C2_NET_QT_MSG_RECV ?
	       success ? &ctx->npc_msg_nr.ntmn_rcvd :
			 &ctx->npc_msg_nr.ntmn_recv_failed :
	       success ? &ctx->npc_msg_nr.ntmn_sent :
			 &ctx->npc_msg_nr.ntmn_send_failed ;
	c2_atomic64_inc(pa64);
}

static void node_ping_timestamp_put(struct c2_net_test_network_ctx *net_ctx,
				    const uint32_t buf_index)
{
}

static c2_time_t
node_ping_timestamp_get(struct c2_net_test_network_ctx *net_ctx,
			const uint32_t buf_index)
{
	return C2_TIME_NEVER;
}

static bool node_ping_buf_enqueue(struct c2_net_test_network_ctx *net_ctx,
				  const uint32_t buf_index,
				  enum c2_net_queue_type q,
				  struct c2_net_end_point *ep,
				  uint32_t ep_index,
				  struct buf_state *bs)
{
	int rc;

	bs->bs_qtype = q;
	rc = q == C2_NET_QT_MSG_SEND ?
		  ep == NULL ?
		  c2_net_test_network_msg_send(net_ctx, buf_index, ep_index) :
		  c2_net_test_network_msg_send_ep(net_ctx, buf_index, ep) :
		  c2_net_test_network_msg_recv(net_ctx, buf_index);
	return (bs->bs_errno = rc) != 0;
}

void node_ping_rtt_add(struct node_ping_ctx *ctx,
		       c2_time_t rtt)
{
	c2_mutex_lock(&ctx->npc_rtt_lock);
	c2_net_test_stats_time_add(&ctx->npc_rtt, rtt);
	c2_mutex_unlock(&ctx->npc_rtt_lock);
}

void node_ping_rtt_stats_get(struct node_ping_ctx *ctx,
			     struct c2_net_test_stats *rtt_stats)
{
	c2_mutex_lock(&ctx->npc_rtt_lock);
	*rtt_stats = ctx->npc_rtt;
	c2_mutex_unlock(&ctx->npc_rtt_lock);
}

/**
 * client msg received
 * - update RTT
 * - add to recv queue
 * client msg sent
 * - set timestamp
 * - add to send queue
 * server msg received
 * - add to send queue
 * server msg sent
 * - add to recv queue
 *
 * client
 * send:
 * - set timestamp
 * - add to send queue
 * send callback:
 * - send
 * receive:
 * - add to recv queue
 * receive callback:
 * - get timestamp, update RTT
 * - receive
 *
 * server:
 * send:
 * - add to send queue
 * send callback:
 * - receive
 * receive:
 * - add to recv queue
 * receive callback:
 * - send
 */

static void node_ping_msg_cb(struct c2_net_test_network_ctx *net_ctx,
			     const uint32_t buf_index,
			     enum c2_net_queue_type q,
			     const struct c2_net_buffer_event *ev)
{
	struct node_ping_ctx *ctx;
	struct buf_state     *bs;
	c2_time_t	      timestamp;
	c2_time_t	      now;
	c2_time_t	      rtt;

	C2_PRE(q == C2_NET_QT_MSG_RECV || q == C2_NET_QT_MSG_SEND);

	ctx = node_ping_ctx_from_net_ctx(net_ctx);
	bs = &ctx->npc_buf_state[buf_index];
	node_ping_msg_stats_update(ctx, bs, q, ev);
	if ((bs->bs_status = ev->nbe_status) != 0 || ctx->npc_test_stop)
		goto failed;

	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT &&
	    q == C2_NET_QT_MSG_RECV) {
		timestamp = node_ping_timestamp_get(net_ctx, buf_index);
		now = c2_time_now();
		if (!c2_time_after(now, timestamp))
			goto failed;
		rtt = c2_time_sub(c2_time_now(), timestamp);
		node_ping_rtt_add(ctx, rtt);
	}

	if (ctx->npc_node_role == C2_NET_TEST_ROLE_SERVER) {
		q = q == C2_NET_QT_MSG_RECV ? C2_NET_QT_MSG_SEND :
					      C2_NET_QT_MSG_RECV;
	}

	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT &&
	    q == C2_NET_QT_MSG_SEND)
		node_ping_timestamp_put(net_ctx, buf_index);
	if (!node_ping_buf_enqueue(net_ctx, buf_index, q,
				   ev->nbe_ep, 0, bs))
		goto failed;
	return;
failed:
	/* we don't want to queue this buffer again */
	c2_semaphore_up(&ctx->npc_buf_q_sem);
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

	c2_mutex_init(&ctx->npc_rtt_lock);
	goto success;

fini:
	ctx = node_ctx->ntnc_svc_private;
	if (ctx->npc_net_initialized)
		c2_net_test_network_ctx_fini(&node_ctx->ntnc_net);
	c2_free(ctx->npc_buf_state);
	c2_mutex_fini(&ctx->npc_rtt_lock);
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
	struct c2_net_test_cmd_status_data sd;
	struct node_ping_ctx		  *ctx;
	c2_time_t			   now;

	C2_PRE(node_ctx != NULL);
	ctx = node_ping_ctx_from_node_ctx(node_ctx);

	/** @todo check 'stop test' conditions */

	/* update bandwidth stats */
	c2_net_test_msg_nr_get_lockfree(&ctx->npc_msg_nr, &sd);
	now = c2_time_now();
	c2_net_test_stats_bandwidth_add(&ctx->npc_bandwidth_1s_send,
					sd.ntcsd_msg_sent * ctx->npc_buf_size,
					now);
	c2_net_test_stats_bandwidth_add(&ctx->npc_bandwidth_1s_recv,
					sd.ntcsd_msg_rcvd * ctx->npc_buf_size,
					now);
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

	ctx->npc_buf_nr	 = cmd->ntc_init.ntci_concurrency;
	ctx->npc_buf_nr *= ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT ?
			   2 * cmd->ntc_init.ntci_ep.ntsl_nr : 1;

	C2_ALLOC_ARR(ctx->npc_buf_state, ctx->npc_buf_nr);
	if (ctx->npc_buf_state == NULL)
		goto reply;
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
		goto free_buf_state;
	/* add test node endpoints to the network context endpoint list */
	for (i = 0; i < cmd->ntc_init.ntci_ep.ntsl_nr; ++i) {
		if ((rc = c2_net_test_network_ep_add(&node_ctx->ntnc_net,
				cmd->ntc_init.ntci_ep.ntsl_list[i])) < 0)
			goto free_net_ctx;
	}
	ctx->npc_net_initialized = true;
	c2_net_test_msg_nr_reset(&ctx->npc_msg_nr);
	c2_net_test_stats_init(&ctx->npc_bandwidth_1s_send.ntsb_stats);
	c2_net_test_stats_init(&ctx->npc_bandwidth_1s_recv.ntsb_stats);
	c2_net_test_stats_init(&ctx->npc_rtt);
	rc = 0;
	goto reply;
free_net_ctx:
	c2_net_test_network_ctx_fini(&node_ctx->ntnc_net);
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
	struct node_ping_ctx *ctx;
	int		      rc;
	int		      i;
	c2_time_t	      one_sec;
	size_t		      concurrency_client;

	C2_PRE(node_ctx != NULL);
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);

	LOGD(__FUNCTION__);

	ctx = node_ping_ctx_from_node_ctx(node_ctx);
	if (ctx->npc_buf_nr == 0) {
		rc = -EINVAL;
		goto reply;
	}
	/* fill test start time */
	ctx->npc_time_start = c2_time_now();
	c2_time_set(&one_sec, 1, 0);
	c2_net_test_stats_bandwidth_init(&ctx->npc_bandwidth_1s_send, 0,
					 ctx->npc_time_start, one_sec);
	c2_net_test_stats_bandwidth_init(&ctx->npc_bandwidth_1s_recv, 0,
					 ctx->npc_time_start, one_sec);
	/* enqueue all buffers */
	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT)
		concurrency_client = ctx->npc_buf_nr /
			             (2 * node_ctx->ntnc_net.ntc_ep_nr);
	for (i = 0; i < ctx->npc_buf_nr; ++i) {
		if (ctx->npc_node_role == C2_NET_TEST_ROLE_SERVER) {
			node_ping_buf_enqueue(&node_ctx->ntnc_net, i,
					      C2_NET_QT_MSG_RECV, NULL, 0,
					      &ctx->npc_buf_state[i]);
		} else {
			if ((i % 2) == 1)
				node_ping_timestamp_put(&node_ctx->ntnc_net, i);
			/* enqueue RECV buffers before SEND buffers */
			node_ping_buf_enqueue(&node_ctx->ntnc_net, i,
					      i % 2 == 0 ? C2_NET_QT_MSG_RECV :
							   C2_NET_QT_MSG_SEND,
					      NULL,
					      i / (2 * concurrency_client),
					      &ctx->npc_buf_state[i]);
		}
		if (ctx->npc_buf_state[i].bs_errno != 0)
			c2_semaphore_up(&ctx->npc_buf_q_sem);
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
	struct node_ping_ctx		   *ctx;
	struct c2_net_test_cmd_status_data *sd;

	C2_PRE(node_ctx != NULL);
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);

	LOGD(__FUNCTION__);

	ctx = node_ping_ctx_from_node_ctx(node_ctx);

	reply->ntc_type = C2_NET_TEST_CMD_STATUS_DATA;
	sd = &reply->ntc_status_data;
	C2_SET0(sd);

	c2_net_test_msg_nr_get_lockfree(&ctx->npc_msg_nr, sd);
	sd->ntcsd_bytes_sent	    = sd->ntcsd_msg_sent * ctx->npc_buf_size;
	sd->ntcsd_bytes_rcvd	    = sd->ntcsd_msg_rcvd * ctx->npc_buf_size;
	sd->ntcsd_time_start	    = ctx->npc_time_start;
	sd->ntcsd_bandwidth_1s_send = ctx->npc_bandwidth_1s_send.ntsb_stats;
	sd->ntcsd_bandwidth_1s_recv = ctx->npc_bandwidth_1s_recv.ntsb_stats;

	node_ping_rtt_stats_get(ctx, &sd->ntcsd_rtt);

	sd->ntcsd_time_now	    = c2_time_now();
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
