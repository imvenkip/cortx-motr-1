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
 * Original creation date: 03/22/2012
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

#include "net/test/client.h"


/**
   @defgroup NetTestStatsBandwidthInternals Bandwidth Statistics
   @ingroup NetTestInternals

   @{
 */

void c2_net_test_stats_bandwidth_init(struct c2_net_test_stats_bandwidth *sb,
				      c2_bcount_t bytes,
				      c2_time_t timestamp,
				      c2_time_t interval)
{
	C2_PRE(sb != NULL);

	c2_net_test_stats_init(&sb->ntsb_stats);

	sb->ntsb_bytes_last    = bytes;
	sb->ntsb_time_last     = timestamp;
	sb->ntsb_time_interval = interval;
}

c2_time_t
c2_net_test_stats_bandwidth_add(struct c2_net_test_stats_bandwidth *sb,
				c2_bcount_t bytes,
				c2_time_t timestamp)
{
	c2_bcount_t   bytes_delta;
	c2_time_t     time_delta;
	c2_time_t     time_next;
	uint64_t      time_delta_ns;
	unsigned long bandwidth;
	unsigned long M_10;		/* M^10 */
	unsigned long M;

	C2_PRE(sb != NULL);
	C2_PRE(bytes >= sb->ntsb_bytes_last);
	C2_PRE(c2_time_after_eq(timestamp, sb->ntsb_time_last));

	bytes_delta = bytes - sb->ntsb_bytes_last;
	time_delta  = c2_time_sub(timestamp, sb->ntsb_time_last);
	time_next   = c2_time_add(timestamp, sb->ntsb_time_interval);

	if (!c2_time_after_eq(timestamp, time_next))
		return time_next;

	sb->ntsb_bytes_last = bytes;
	/** @todo problem with small sb->ntsb_time_interval can be here */
	sb->ntsb_time_last  = time_next;

	time_delta_ns = c2_time_seconds(time_delta) * C2_TIME_ONE_BILLION +
			c2_time_nanoseconds(time_delta);
	/*
	   To measure bandwidth in bytes/sec it needs to be calculated
	   (bytes_delta / time_delta_ns) * 1'000'000'000 =
	   (bytes_delta * 1'000'000'000) / time_delta_ns =
	   ((bytes_delta * (10^M)) / time_delta_ns) * (10^(9-M)),
	   where M is some parameter. To perform integer division M
	   should be maximized in range [0, 9] - in case if M < 9
	   there is a loss of precision.
	 */
	if (C2_BCOUNT_MAX / C2_TIME_ONE_BILLION > bytes_delta) {
		/* simple case. M = 9 */
		bandwidth = bytes_delta * C2_TIME_ONE_BILLION / time_delta_ns;
	} else {
		/* harder case. M is in range [0, 9) */
		M_10 = 1;
		for (M = 0; M < 8; ++M) {
			if (C2_BCOUNT_MAX / (M_10 * 10) > bytes_delta)
				M_10 *= 10;
			else
				break;
		}
		/* M is maximized */
		bandwidth = (bytes_delta * M_10 / time_delta_ns) *
			    (C2_TIME_ONE_BILLION / M_10);
	}
	c2_net_test_stats_add(&sb->ntsb_stats, bandwidth);

	return time_next;
}

/**
   @} end of NetTestStatsBandwidthInternals group
 */

/**
   @defgroup NetTestMsgNRInternals Messages Number
   @ingroup NetTestInternals

   @{
 */

void c2_net_test_msg_nr_reset(struct c2_net_test_msg_nr *msg_nr)
{
	c2_atomic64_set(&msg_nr->ntmn_sent, 0);
	c2_atomic64_set(&msg_nr->ntmn_rcvd, 0);
	c2_atomic64_set(&msg_nr->ntmn_send_failed, 0);
	c2_atomic64_set(&msg_nr->ntmn_recv_failed, 0);
}

void c2_net_test_msg_nr_get_lockfree(struct c2_net_test_msg_nr *msg_nr,
				     struct c2_net_test_cmd_status_data *sd)
{
	uint64_t sent;
	uint64_t rcvd;
	uint64_t send_failed;
	uint64_t recv_failed;

	C2_PRE(msg_nr != NULL);
	C2_PRE(sd);

	do {
		sent	    = sd->ntcsd_msg_sent;
		rcvd	    = sd->ntcsd_msg_rcvd;
		send_failed = sd->ntcsd_msg_send_failed;
		recv_failed = sd->ntcsd_msg_recv_failed;
		sd->ntcsd_msg_sent = c2_atomic64_get(&msg_nr->ntmn_sent);
		sd->ntcsd_msg_rcvd = c2_atomic64_get(&msg_nr->ntmn_rcvd);
		sd->ntcsd_msg_send_failed =
			c2_atomic64_get(&msg_nr->ntmn_send_failed);
		sd->ntcsd_msg_recv_failed =
			c2_atomic64_get(&msg_nr->ntmn_recv_failed);
	} while (sent	     != sd->ntcsd_msg_sent ||
		 rcvd	     != sd->ntcsd_msg_rcvd ||
		 send_failed != sd->ntcsd_msg_send_failed ||
		 recv_failed != sd->ntcsd_msg_recv_failed);
}

/**
   @} end of NetTestMsgNRInternals group
 */

/**
   @defgroup NetTestPingNodeInternals Ping Node
   @ingroup NetTestInternals

   @todo split this file.

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
};

/** Ping node context */
struct node_ping_ctx {
	/** Node context */
	struct c2_net_test_node_ctx	  *npc_node_ctx;
	/** Node role */
	enum c2_net_test_role		   npc_node_role;
	/** Number of network buffers to send/receive test messages. */
	size_t				   npc_buf_nr;
	/** Size of network buffers. */
	c2_bcount_t			   npc_buf_size;
	/**
	 * Test messages concurrency.
	 * @see c2_net_test_cmd_init.ntci_concurrency
	 */
	size_t				   npc_concurrency;
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

static void node_ping_msg_stats_update(struct node_ping_ctx *ctx,
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
		  c2_net_test_network_msg_send_ep(net_ctx, buf_index, ep) :
		  c2_net_test_network_msg_send(net_ctx, buf_index, ep_index) :
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
	node_ctx->ntnc_svc_private = ctx;
	if (ctx == NULL)
		goto failed;
	ctx->npc_node_ctx = node_ctx;

	rc = c2_semaphore_init(&ctx->npc_buf_q_sem, 0);
	if (rc != 0)
		goto free_ping_ctx;

	c2_mutex_init(&ctx->npc_rtt_lock);
	goto success;

fini:
	ctx = node_ctx->ntnc_svc_private;
	if (ctx->npc_net_initialized)
		c2_net_test_network_ctx_fini(&node_ctx->ntnc_net);
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
	/* parse INIT command */
	ctx->npc_node_role	  = cmd->ntc_init.ntci_role;
	ctx->npc_buf_size	  = cmd->ntc_init.ntci_msg_size;
	ctx->npc_buf_send_timeout = cmd->ntc_init.ntci_buf_send_timeout;

	ctx->npc_buf_nr	 = cmd->ntc_init.ntci_concurrency;
	ctx->npc_buf_nr *= ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT ?
			   2 * cmd->ntc_init.ntci_ep.ntsl_nr : 1;
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
		goto reply;
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

	C2_PRE(node_ctx != NULL);
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);

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
	for (i = 0; i < ctx->npc_buf_nr; ++i) {
		if (ctx->npc_node_role == C2_NET_TEST_ROLE_SERVER) {
			node_ping_buf_enqueue(&node_ctx->ntnc_net, i,
					      C2_NET_QT_MSG_RECV, NULL, 0,
					      &ctx->npc_buf_state[i]);
		} else {
			if ((i % 2) == 0)
				node_ping_timestamp_put(&node_ctx->ntnc_net, i);
			node_ping_buf_enqueue(&node_ctx->ntnc_net, i,
					      i % 2 == 0 ? C2_NET_QT_MSG_SEND :
							   C2_NET_QT_MSG_RECV,
					      NULL,
					      i / (2 * ctx->npc_concurrency),
					      &ctx->npc_buf_state[i]);
		}
		if (ctx->npc_buf_state[i].bs_errno != 0)
			c2_semaphore_up(&ctx->npc_buf_q_sem);
	}
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

/**
   @defgroup NetTestClientInternals Test Client
   @ingroup NetTestInternals

   @see
   @ref net-test

   @{
 */

enum {
	NODE_WAIT_CMD_GRANULARITY_MS = 20,
};

static void node_tm_event_cb(const struct c2_net_tm_event *ev)
{
}

static const struct c2_net_tm_callbacks node_tm_cb = {
	.ntc_event_cb = node_tm_event_cb
};

static struct c2_net_test_service_ops
*service_ops_get(struct c2_net_test_cmd *cmd)
{
	C2_PRE(cmd->ntc_type == C2_NET_TEST_CMD_INIT);

	if (cmd->ntc_init.ntci_type == C2_NET_TEST_TYPE_PING) {
		return &c2_net_test_node_ping_ops;
	} else {
		return NULL;
	}
}

static int node_cmd_get(struct c2_net_test_cmd_ctx *cmd_ctx,
			struct c2_net_test_cmd *cmd,
			c2_time_t deadline)
{
	int rc = c2_net_test_commands_recv(cmd_ctx, cmd, deadline);
	if (rc == 0)
		rc = c2_net_test_commands_recv_enqueue(cmd_ctx,
						       cmd->ntc_buf_index);
	return rc;
}

static int node_cmd_wait(struct c2_net_test_node_ctx *ctx,
			 struct c2_net_test_cmd *cmd,
			 enum c2_net_test_cmd_type type)
{
	c2_time_t deadline;
	const int TIME_ONE_MS = C2_TIME_ONE_BILLION / 1000;
	int	  rc;

	C2_PRE(ctx != NULL);
	do {
		deadline = c2_time_from_now(0, NODE_WAIT_CMD_GRANULARITY_MS *
					       TIME_ONE_MS);
		rc = node_cmd_get(&ctx->ntnc_cmd, cmd, deadline);
		if (rc != 0 && rc != -ETIMEDOUT)
			return rc;	/** @todo add retry count */
	} while (cmd->ntc_type != type && !ctx->ntnc_exit_flag);
	return 0;
}

static void node_thread(struct c2_net_test_node_ctx *ctx)
{
	struct c2_net_test_service	svc;
	struct c2_net_test_service_ops *svc_ops;
	enum c2_net_test_service_state	svc_state;
	struct c2_net_test_cmd		cmd;
	struct c2_net_test_cmd		reply;
	int				rc;

	C2_PRE(ctx != NULL);

	/* wait for INIT command */
	rc = node_cmd_wait(ctx, &cmd, C2_NET_TEST_CMD_INIT);
	if (ctx->ntnc_exit_flag) {
		c2_net_test_commands_received_free(&cmd);
		return;
	}
	if ((ctx->ntnc_errno = rc) != 0)
		return;
	/* we have configuration; initialize test service */
	svc_ops = service_ops_get(&cmd);
	if (svc_ops == NULL) {
		c2_net_test_commands_received_free(&cmd);
		return;
	}
	rc = c2_net_test_service_init(&svc, ctx, svc_ops);
	if (rc != 0) {
		c2_net_test_commands_received_free(&cmd);
		return;
	}
	/* handle INIT command inside main loop */
	rc = -EINPROGRESS;
	/* test service is initialized. start main loop */
	do {
		/* get command */
		if (rc == 0)
			rc = node_cmd_get(&ctx->ntnc_cmd, &cmd, c2_time_now());
		if (rc == 0 && cmd.ntc_ep_index >= 0) {
			/* we have command. handle it */
			rc = c2_net_test_service_cmd_handle(&svc, &cmd, &reply);
			reply.ntc_ep_index = cmd.ntc_ep_index;
			c2_net_test_commands_received_free(&cmd);
			/* send reply */
			c2_net_test_commands_send_wait_all(&ctx->ntnc_cmd);
			c2_net_test_commands_send(&ctx->ntnc_cmd, &reply);
		} else if (rc == -ETIMEDOUT) {
			/* we haven't command. take a step. */
			rc = c2_net_test_service_step(&svc);
		} else {
			break;
		}
		svc_state = c2_net_test_service_state_get(&svc);
	} while (svc_state != C2_NET_TEST_SERVICE_FAILED &&
		 svc_state != C2_NET_TEST_SERVICE_FINISHED &&
		 !ctx->ntnc_exit_flag &&
		 rc == 0);

	ctx->ntnc_errno = rc;
	/* finalize test service */
	c2_net_test_service_fini(&svc);

	c2_semaphore_up(&ctx->ntnc_thread_finished_sem);
}

static int node_init_fini(struct c2_net_test_node_ctx *ctx,
			  struct c2_net_test_node_cfg *cfg,
			  bool init)
{
	struct c2_net_test_slist ep_list;
	int			 rc;

	C2_PRE(ctx != NULL);
	C2_PRE(ergo(init, cfg != NULL));
	if (!init)
		goto fini;

	rc = c2_net_test_slist_init(&ep_list, cfg->ntnc_addr_console, '`');
	if (rc != 0)
		goto failed;
	rc = c2_net_test_commands_init(&ctx->ntnc_cmd,
				       cfg->ntnc_addr,
				       cfg->ntnc_send_timeout,
				       NULL,
				       &ep_list);
	c2_net_test_slist_fini(&ep_list);
	if (rc != 0)
		goto failed;
	rc = c2_semaphore_init(&ctx->ntnc_thread_finished_sem, 0);
	if (rc != 0)
		goto commands_fini;

	return 0;
fini:
	rc = 0;
	c2_semaphore_fini(&ctx->ntnc_thread_finished_sem);
commands_fini:
	c2_net_test_commands_fini(&ctx->ntnc_cmd);
failed:
	return rc;
}

int c2_net_test_node_init(struct c2_net_test_node_ctx *ctx,
			  struct c2_net_test_node_cfg *cfg)
{
	return node_init_fini(ctx, cfg, true);
}

void c2_net_test_node_fini(struct c2_net_test_node_ctx *ctx)
{
	int rc = node_init_fini(ctx, NULL, false);
	C2_POST(rc == 0);
}

int c2_net_test_node_start(struct c2_net_test_node_ctx *ctx)
{
	int rc;

	C2_PRE(ctx != NULL);

	ctx->ntnc_exit_flag = false;
	ctx->ntnc_errno	    = 0;

	rc = C2_THREAD_INIT(&ctx->ntnc_thread, struct c2_net_test_node_ctx *,
			    NULL, &node_thread, ctx, "net_test_node_thread");
	return rc;
}

void c2_net_test_node_stop(struct c2_net_test_node_ctx *ctx)
{
	int rc;

	C2_PRE(ctx != NULL);

	ctx->ntnc_exit_flag = true;
	c2_net_test_commands_send_wait_all(&ctx->ntnc_cmd);
	rc = c2_thread_join(&ctx->ntnc_thread);
	/*
	 * In either case when rc != 0 there is an unmatched
	 * c2_net_test_node_start() and c2_net_test_node_stop()
	 * or deadlock. If non-zero rc is returned as result of this function,
	 * then c2_net_test_node_stop() leaves c2_net_test_node_ctx in
	 * inconsistent state (also possible resource leak).
	 */
	C2_ASSERT(rc == 0);
	c2_thread_fini(&ctx->ntnc_thread);
}

struct c2_net_test_node_ctx
*c2_net_test_node_ctx_from_net_ctx(struct c2_net_test_network_ctx *net_ctx)
{
	return (struct c2_net_test_node_ctx *)
	       ((char *) net_ctx -
		offsetof(struct c2_net_test_node_ctx, ntnc_net));
}

/**
   @} end of NetTestClientInternals group
 */

/**
   @defgroup NetTestConsoleInternals Test Console
   @ingroup NetTestInternals

   @see
   @ref net-test

   @{
 */

static int console_role_init_fini(struct c2_net_test_console_role_ctx *ctx,
				 struct c2_net_test_console_cfg *cfg,
				 enum c2_net_test_role role,
				 bool init)
{
	struct c2_net_test_slist *nodes;
	char			 *addr_console;
	int			  rc;

	if (!init)
		goto fini;

	addr_console = role == C2_NET_TEST_ROLE_CLIENT ?
		cfg->ntcc_addr_console4clients : cfg->ntcc_addr_console4servers;
	nodes = role == C2_NET_TEST_ROLE_CLIENT ?
		&cfg->ntcc_clients : &cfg->ntcc_servers;

	C2_ALLOC_PTR(ctx->ntcrc_cmd);
	if (ctx->ntcrc_cmd == NULL)
		goto fail;
	C2_ALLOC_PTR(ctx->ntcrc_sd);
	if (ctx->ntcrc_sd == NULL)
		goto fini_cmd;
	ctx->ntcrc_nr = nodes->ntsl_nr;
	C2_ALLOC_ARR(ctx->ntcrc_errno, ctx->ntcrc_nr);
	if (ctx->ntcrc_errno == NULL)
		goto fini_sd;
	C2_ALLOC_ARR(ctx->ntcrc_status, ctx->ntcrc_nr);
	if (ctx->ntcrc_status == NULL)
		goto fini_errno;

	rc = c2_net_test_commands_init(ctx->ntcrc_cmd, addr_console,
				       cfg->ntcc_cmd_send_timeout, NULL, nodes);
	if (rc != 0)
		goto fini_status;

	rc = 0;
	goto success;

fini:
	rc = 0;
	c2_net_test_commands_fini(ctx->ntcrc_cmd);
fini_status:
	c2_free(ctx->ntcrc_status);
fini_errno:
	c2_free(ctx->ntcrc_errno);
fini_sd:
	c2_free(ctx->ntcrc_sd);
fini_cmd:
	c2_free(ctx->ntcrc_cmd);
fail:
success:
	return rc;
}

static int console_init_fini(struct c2_net_test_console_ctx *ctx,
			     struct c2_net_test_console_cfg *cfg,
			     bool init)
{
	int rc;

	C2_PRE(ctx != NULL);
	C2_PRE(ergo(init, cfg != NULL));

	ctx->ntcc_cfg = cfg;
	rc = console_role_init_fini(&ctx->ntcc_clients, cfg,
				    C2_NET_TEST_ROLE_CLIENT, init);
	if (rc == 0)
		rc = console_role_init_fini(&ctx->ntcc_servers, cfg,
					    C2_NET_TEST_ROLE_SERVER, init);
	return rc;
}

int c2_net_test_console_init(struct c2_net_test_console_ctx *ctx,
			     struct c2_net_test_console_cfg *cfg)
{
	return console_init_fini(ctx, cfg, true);
}

void c2_net_test_console_fini(struct c2_net_test_console_ctx *ctx)
{
	int rc = console_init_fini(ctx, NULL, false);
	C2_POST(rc == 0);
}

static void console_cmd_init_fill(struct c2_net_test_console_cfg *cfg,
				  enum c2_net_test_role role,
				  struct c2_net_test_cmd_init *cinit)
{
	cinit->ntci_role	     = role;
	cinit->ntci_type	     = cfg->ntcc_test_type;
	cinit->ntci_msg_nr	     = cfg->ntcc_msg_nr;
	cinit->ntci_msg_size	     = cfg->ntcc_msg_size;
	cinit->ntci_concurrency      = role == C2_NET_TEST_ROLE_CLIENT ?
				       cfg->ntcc_concurrency_client :
				       cfg->ntcc_concurrency_server;
	cinit->ntci_buf_send_timeout = cfg->ntcc_buf_send_timeout;
	cinit->ntci_ep		     = role == C2_NET_TEST_ROLE_CLIENT ?
				       cfg->ntcc_servers : cfg->ntcc_clients;
}

static void status_data_add(struct c2_net_test_cmd_status_data *sd,
			    const struct c2_net_test_cmd_status_data *cmd_sd)
{
	c2_net_test_stats_add_stats(&sd->ntcsd_bandwidth_1s_send,
				    &cmd_sd->ntcsd_bandwidth_1s_send);
	c2_net_test_stats_add_stats(&sd->ntcsd_bandwidth_1s_recv,
				    &cmd_sd->ntcsd_bandwidth_1s_recv);
	c2_net_test_stats_add_stats(&sd->ntcsd_rtt,
				    &cmd_sd->ntcsd_rtt);
}

size_t c2_net_test_console_cmd(struct c2_net_test_console_ctx *ctx,
			       enum c2_net_test_role role,
			       enum c2_net_test_cmd_type cmd_type)
{
	struct c2_net_test_console_role_ctx *rctx;
	struct c2_net_test_console_cfg	    *cfg;
	struct c2_net_test_cmd_ctx	    *cmd_ctx;
	struct c2_net_test_cmd		     cmd;
	struct c2_net_test_cmd_status_data  *sd;
	int				     i;
	int				     j;
	int				     rc;
	struct c2_net_test_slist	    *nodes;
	bool				     role_client;
	c2_time_t			     deadline;
	size_t				     success_nr = 0;
	enum c2_net_test_cmd_type	     answer[] = {
		[C2_NET_TEST_CMD_INIT]	 = C2_NET_TEST_CMD_INIT_DONE,
		[C2_NET_TEST_CMD_START]	 = C2_NET_TEST_CMD_START_DONE,
		[C2_NET_TEST_CMD_STOP]	 = C2_NET_TEST_CMD_STOP_DONE,
		[C2_NET_TEST_CMD_STATUS] = C2_NET_TEST_CMD_STATUS_DATA,
	};

	C2_PRE(ctx != NULL);
	C2_PRE(role == C2_NET_TEST_ROLE_SERVER ||
	       role == C2_NET_TEST_ROLE_CLIENT);
	C2_PRE(cmd_type == C2_NET_TEST_CMD_INIT ||
	       cmd_type == C2_NET_TEST_CMD_START ||
	       cmd_type == C2_NET_TEST_CMD_STOP ||
	       cmd_type == C2_NET_TEST_CMD_STATUS);

	C2_SET0(&cmd);
	cfg = ctx->ntcc_cfg;

	if (cmd_type == C2_NET_TEST_CMD_INIT)
		console_cmd_init_fill(cfg, role, &cmd.ntc_init);

	role_client  = role == C2_NET_TEST_ROLE_CLIENT;
	cmd.ntc_type = cmd_type;
	nodes	     = role_client ? &cfg->ntcc_clients : &cfg->ntcc_servers;
	rctx	     = role_client ? &ctx->ntcc_clients : &ctx->ntcc_servers;
	cmd_ctx	     = rctx->ntcrc_cmd;

	/* send all commands */
	for (i = 0; i < nodes->ntsl_nr; ++i) {
		if (cmd_type == C2_NET_TEST_CMD_INIT)
			cmd.ntc_init.ntci_tm_ep = nodes->ntsl_list[i];
		cmd.ntc_ep_index     = i;
		rctx->ntcrc_errno[i] = c2_net_test_commands_send(cmd_ctx, &cmd);
	}
	c2_net_test_commands_send_wait_all(cmd_ctx);

	/* receive answers */
	deadline = c2_time_add(c2_time_now(), cfg->ntcc_cmd_recv_timeout);
	if (answer[cmd_type] == C2_NET_TEST_CMD_STATUS_DATA) {
		sd = rctx->ntcrc_sd;
		C2_SET0(sd);
		c2_net_test_stats_init(&sd->ntcsd_bandwidth_1s_send);
		c2_net_test_stats_init(&sd->ntcsd_bandwidth_1s_recv);
		c2_net_test_stats_init(&sd->ntcsd_rtt);
	}
	while (!c2_time_after(c2_time_now(), deadline)) {
		rc = c2_net_test_commands_recv(cmd_ctx, &cmd, deadline);
		/* deadline reached */
		if (rc == -ETIMEDOUT)
			break;
		/** @todo possible spinlock if all recv fails instantly? */
		if (rc != 0)
			continue;
		/* reject unknown sender */
		if ((j = cmd.ntc_ep_index) < 0)
			goto reuse_cmd;
		/* reject unexpected command type */
		if (cmd.ntc_type != answer[cmd_type])
			goto reuse_cmd;
		/* reject command from node, which can't have incoming cmd */
		C2_ASSERT(j < nodes->ntsl_nr);
		if (rctx->ntcrc_errno[j] != 0)
			goto reuse_cmd;
		/* handle incoming command */
		if (answer[cmd_type] == C2_NET_TEST_CMD_STATUS_DATA)
			status_data_add(sd, &cmd.ntc_status_data);
		/* if received errno == 0 */
		if ((rctx->ntcrc_status[j] = cmd.ntc_done.ntcd_errno) == 0)
			success_nr++;
		/*
		 * @todo console user can't recover from this error -
		 * cmd.ntc_buf_index is lost. use ringbuf to save?
		 */
reuse_cmd:
		rctx->ntcrc_errno[j] =
			c2_net_test_commands_recv_enqueue(cmd_ctx,
							  cmd.ntc_buf_index);
		c2_net_test_commands_received_free(&cmd);
	}

	return success_nr;
}

/**
   @} end of NetTestConsoleInternals group
 */

/**
   @defgroup NetTestServiceInternals Test Service
   @ingroup NetTestInternals

   @see
   @ref net-test

   @{
 */

/** Service state transition matrix. @see net-test-lspec-state */
static bool state_transition[C2_NET_TEST_SERVICE_NR][C2_NET_TEST_SERVICE_NR] = {
	[C2_NET_TEST_SERVICE_UNINITIALIZED] = {
		[C2_NET_TEST_SERVICE_UNINITIALIZED] = false,
		[C2_NET_TEST_SERVICE_READY]	    = true,
		[C2_NET_TEST_SERVICE_FINISHED]	    = false,
		[C2_NET_TEST_SERVICE_FAILED]	    = false,
	},
	[C2_NET_TEST_SERVICE_READY] = {
		[C2_NET_TEST_SERVICE_UNINITIALIZED] = true,
		[C2_NET_TEST_SERVICE_READY]	    = false,
		[C2_NET_TEST_SERVICE_FINISHED]	    = true,
		[C2_NET_TEST_SERVICE_FAILED]	    = true,
	},
	[C2_NET_TEST_SERVICE_FINISHED] = {
		[C2_NET_TEST_SERVICE_UNINITIALIZED] = true,
		[C2_NET_TEST_SERVICE_READY]	    = false,
		[C2_NET_TEST_SERVICE_FINISHED]	    = false,
		[C2_NET_TEST_SERVICE_FAILED]	    = false,
	},
	[C2_NET_TEST_SERVICE_FAILED] = {
		[C2_NET_TEST_SERVICE_UNINITIALIZED] = true,
		[C2_NET_TEST_SERVICE_READY]	    = false,
		[C2_NET_TEST_SERVICE_FINISHED]	    = false,
		[C2_NET_TEST_SERVICE_FAILED]	    = false,
	},
};

int c2_net_test_service_init(struct c2_net_test_service *svc,
			     struct c2_net_test_node_ctx *node_ctx,
			     struct c2_net_test_service_ops *ops)
{
	C2_PRE(svc != NULL);
	C2_PRE(node_ctx != NULL);
	C2_PRE(ops != NULL);

	C2_SET0(svc);
	node_ctx->ntnc_svc = svc;
	svc->nts_node_ctx  = node_ctx;
	svc->nts_ops	   = ops;

	svc->nts_errno = svc->nts_ops->ntso_init(svc->nts_node_ctx);
	if (svc->nts_errno == 0)
		c2_net_test_service_state_change(svc,
				C2_NET_TEST_SERVICE_READY);

	C2_POST(ergo(svc->nts_errno == 0, c2_net_test_service_invariant(svc)));

	return svc->nts_errno;
}

void c2_net_test_service_fini(struct c2_net_test_service *svc)
{
	C2_PRE(c2_net_test_service_invariant(svc));
	C2_PRE(svc->nts_state != C2_NET_TEST_SERVICE_UNINITIALIZED);

	svc->nts_ops->ntso_fini(svc->nts_node_ctx);
	c2_net_test_service_state_change(svc,
			C2_NET_TEST_SERVICE_UNINITIALIZED);
}

bool c2_net_test_service_invariant(struct c2_net_test_service *svc)
{
	if (svc == NULL)
		return false;
	if (svc->nts_ops == NULL)
		return false;
	return true;
}

int c2_net_test_service_step(struct c2_net_test_service *svc)
{
	C2_PRE(c2_net_test_service_invariant(svc));
	C2_PRE(svc->nts_state == C2_NET_TEST_SERVICE_READY);

	svc->nts_errno = svc->nts_ops->ntso_step(svc->nts_node_ctx);
	if (svc->nts_errno != 0)
		c2_net_test_service_state_change(svc,
				C2_NET_TEST_SERVICE_FAILED);

	C2_POST(c2_net_test_service_invariant(svc));
	return svc->nts_errno;
}

int c2_net_test_service_cmd_handle(struct c2_net_test_service *svc,
				   struct c2_net_test_cmd *cmd,
				   struct c2_net_test_cmd *reply)
{
	struct c2_net_test_service_cmd_handler *handler;
	int					i;

	C2_PRE(c2_net_test_service_invariant(svc));
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);
	C2_PRE(svc->nts_state == C2_NET_TEST_SERVICE_READY);

	svc->nts_errno = -ENOENT;
	for (i = 0; i < svc->nts_ops->ntso_cmd_handler_nr; ++i) {
		handler = &svc->nts_ops->ntso_cmd_handler[i];
		if (handler->ntsch_type == cmd->ntc_type) {
			svc->nts_errno = handler->ntsch_handler(
					 svc->nts_node_ctx, cmd, reply);
			break;
		}
	}

	C2_POST(c2_net_test_service_invariant(svc));
	return svc->nts_errno;
}

void c2_net_test_service_state_change(struct c2_net_test_service *svc,
				      enum c2_net_test_service_state state)
{
	C2_PRE(c2_net_test_service_invariant(svc));

	C2_ASSERT(state_transition[svc->nts_state][state]);
	svc->nts_state = state;

	C2_POST(c2_net_test_service_invariant(svc));
}

enum c2_net_test_service_state
c2_net_test_service_state_get(struct c2_net_test_service *svc)
{
	C2_PRE(c2_net_test_service_invariant(svc));

	return svc->nts_state;
}

/**
   @} end of NetTestServiceInternals group
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
