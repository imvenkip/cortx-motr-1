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

/* @todo remove */
#ifndef __KERNEL__
#include <stdio.h>		/* printf */
#endif

/* @todo debug only, remove it */
#ifndef __KERNEL__
/*
#define LOGD(format, ...) printf(format, ##__VA_ARGS__)
*/
#define LOGD(format, ...) do {} while (0)
#else
#define LOGD(format, ...) do {} while (0)
#endif

#include "lib/memory.h"		/* C2_ALLOC_PTR */
#include "lib/misc.h"		/* C2_SET0 */
#include "lib/time.h"		/* c2_time_t */
#include "lib/errno.h"		/* ENOMEM */
#include "lib/thread.h"		/* C2_THREAD_INIT */
#include "lib/tlist.h"		/* c2_tlist */

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
	/** @todo change after merging to master and move to lib/magic.h */
	BS_LINK_MAGIC	  = 0x1213141516,
	/** @todo change after merging to master and move to lib/magic.h */
	BS_HEAD_MAGIC	  = 0x1718191A1B,
	/** Timeout checking interval, ms */
	TO_CHECK_INTERVAL = 10,
};

/** Buffer state */
struct buf_state {
	/** Queue type */
	enum c2_net_queue_type	   bs_q;
	/**
	 * Result of last buffer enqueue operation.
	 * Have non-zero value at the start of test.
	 */
	int			   bs_errno;
	/**
	 * Copy of network buffer event.
	 * Makes no sense if bs_errno != 0.
	 * c2_net_buffer_event.nbe_ep will be set in buffer callback
	 * using c2_net_end_point_get().
	 */
	struct c2_net_buffer_event bs_ev;
	/** Callback executing time */
	c2_time_t		   bs_time;
	/** Sequence number for test client messages */
	uint64_t		   bs_seq;
	/** Deadline for messages on the test client */
	c2_time_t		   bs_deadline;
	/**
	 * Number of executed callbacks for the test message
	 * (sent and received) on the test client.
	 */
	uint64_t		   bs_cb_nr;
	/**
	 * Index of corresponding recv buffer for send buffer
	 * and send buffer for recv buffer;
	 * (test client only).
	 */
	size_t			   bs_index_pair;
	/** Link for messages timeout list */
	struct c2_tlink		   bs_link;
	/** Magic for typed list */
	uint64_t		   bs_link_magic;
};

C2_TL_DESCR_DEFINE(buf_state, "buf_state", static, struct buf_state, bs_link,
		   bs_link_magic, BS_LINK_MAGIC, BS_HEAD_MAGIC);
C2_TL_DEFINE(buf_state, static, struct buf_state);

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
	size_t	     npcc_msg_sent;
	/**
	 * Number of test messages sent to test server and received back
	 * (including failed) for the test client.
	 */
	size_t	     npcc_msg_rt;
	/**
	 * Maximum number of sent to test server and received back
	 * test messages (for the test client). "rt" == "round-trip".
	 */
	size_t	     npcc_msg_rt_max;
	/** Concurrency. @see c2_net_test_cmd_init.ntci_concurrency */
	size_t	     npcc_concurrency;
	/** Messages timeout list */
	struct c2_tl npcc_to;
};

/** Test server context */
struct node_ping_server_ctx {
	int npsc_unused;
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
	/** Test was initialized (succesful node_ping_cmd_start() */
	bool				    npc_test_initialized;
	/** All needed statistics */
	struct c2_net_test_cmd_status_data  npc_status_data;
	/** @todo use spinlock instead of mutex
	 *  @todo make copy of status data, protect it with mutex.
	 *  N times per secound update this copy from original status data,
	 *  but leave original status data updates without mutex.
	 */
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
	/** Ringbuf of buffers that are not in network buffers queue */
	struct c2_net_test_ringbuf	    npc_buf_rb;
	/** up() after adding buffer to queue, down() before receiving */
	struct c2_semaphore		    npc_buf_rb_sem;
	/** Previous up() was from node_ping_cmd_stop() */
	bool				    npc_buf_rb_done;
	/** Array of buffer states */
	struct buf_state		   *npc_buf_state;
	/* Worker thread */
	struct c2_thread		    npc_thread;
	union {
		struct node_ping_client_ctx npc__client;
		struct node_ping_server_ctx npc__server;
	};
	/**
	 * Client context. Set to NULL on the test server,
	 * set to &node_ping_ctx.npc__client on the test client.
	 */
	struct node_ping_client_ctx	   *npc_client;
	/**
	 * Server context. See npc_client.
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

static c2_time_t node_ping_timestamp_put(struct c2_net_test_network_ctx *net_ctx,
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
	return ts.ntt_time;
}

static bool node_ping_timestamp_get(struct c2_net_test_network_ctx *net_ctx,
				    uint32_t buf_index,
				    struct c2_net_test_timestamp *ts)
{
	struct c2_net_buffer *buf;
	c2_bcount_t	      len;

	C2_PRE(net_ctx != NULL);
	C2_PRE(ts != NULL);

	buf = c2_net_test_network_buf(net_ctx, C2_NET_TEST_BUF_PING, buf_index);
	len = c2_net_test_timestamp_serialize(C2_NET_TEST_DESERIALIZE, ts,
					      &buf->nb_buffer, 0);
	return len != 0;
}

static void node_ping_to_add(struct node_ping_ctx *ctx,
			     size_t buf_index)
{
	LOGD(">> WAIT: >> %s: buf_index = %lu\n",
	     __FUNCTION__, buf_index);
	buf_state_tlist_add_tail(&ctx->npc_client->npcc_to,
				 &ctx->npc_buf_state[buf_index]);
}

static void node_ping_to_del(struct node_ping_ctx *ctx,
			     size_t buf_index)
{
	LOGD(">> WAIT: >> %s: buf_index = %lu\n",
	     __FUNCTION__, buf_index);
	buf_state_tlist_del(&ctx->npc_buf_state[buf_index]);
}

static ssize_t node_ping_to_peek(struct node_ping_ctx *ctx)
{
	struct buf_state *bs;
	ssize_t		  buf_index;

	bs = buf_state_tlist_head(&ctx->npc_client->npcc_to);
	buf_index = bs == NULL ? -1 : bs - ctx->npc_buf_state;

	C2_POST(buf_index == -1 ||
	        (buf_index >= 0 && buf_index < ctx->npc_buf_nr));
	return buf_index;
}

static ssize_t node_ping_client_search_seq(struct node_ping_ctx *ctx,
					   size_t server_index,
					   uint64_t seq)
{
	size_t i;
	size_t buf_index;
	size_t concurrency = ctx->npc_client->npcc_concurrency;

	for (i = 0; i < concurrency; ++i) {
		buf_index = concurrency * server_index + i;
		if (ctx->npc_buf_state[buf_index].bs_seq == seq)
			return buf_index;
	}
	return -1;
}

static void node_ping_buf_enqueue(struct node_ping_ctx *ctx,
				  size_t buf_index,
				  enum c2_net_queue_type q,
				  struct c2_net_end_point *ep,
				  size_t ep_index)
{
	struct c2_net_test_network_ctx *nctx = &ctx->npc_net;
	struct buf_state	       *bs = &ctx->npc_buf_state[buf_index];
	bool				decreased;

	C2_PRE(ergo(ep != NULL, q == C2_NET_QT_MSG_SEND));

	decreased = c2_semaphore_trydown(&ctx->npc_buf_q_sem);
	if (!decreased) {
		/* worker thread is stopping */
		C2_ASSERT(ctx->npc_buf_rb_done);
		bs->bs_errno = -EWOULDBLOCK;
		return;
	}
	LOGD("node_ping_buf_enqueue:");
	LOGD(", q = %d", q);
	LOGD(", buf_index = %lu", buf_index);
	LOGD(", ep_index = %lu", ep_index);
	LOGD(", %s", ctx->npc_net.ntc_tm->ntm_ep->nep_addr);
	if (q == C2_NET_QT_MSG_SEND) {
		LOGD(" => %s", (ep == NULL ?
			    c2_net_test_network_ep(&ctx->npc_net, ep_index) :
			    ep)->nep_addr);
	} else {
		LOGD(" <= ");
	}
	LOGD("\n");
	bs->bs_errno = (bs->bs_q = q) == C2_NET_QT_MSG_RECV ?
		  c2_net_test_network_msg_recv(nctx, buf_index) : ep == NULL ?
		  c2_net_test_network_msg_send(nctx, buf_index, ep_index) :
		  c2_net_test_network_msg_send_ep(nctx, buf_index, ep);
	if (bs->bs_errno != 0) {
		c2_net_test_ringbuf_push(&ctx->npc_buf_rb, buf_index);
		c2_semaphore_up(&ctx->npc_buf_q_sem);
		c2_semaphore_up(&ctx->npc_buf_rb_sem);
	}
}

static void node_ping_buf_enqueue_recv(struct node_ping_ctx *ctx,
				       size_t buf_index)
{
	node_ping_buf_enqueue(ctx, buf_index, C2_NET_QT_MSG_RECV, NULL, 0);
}

static void node_ping_client_send(struct node_ping_ctx *ctx,
				  size_t buf_index)
{
	struct node_ping_client_ctx *cctx;
	struct buf_state	    *bs;
	size_t			     ep_index;
	c2_time_t		     begin;

	C2_PRE(ctx != NULL && ctx->npc_client != NULL);
	C2_PRE(buf_index < ctx->npc_buf_nr / 2);

	bs	 = &ctx->npc_buf_state[buf_index];
	cctx	 = ctx->npc_client;
	ep_index = buf_index / cctx->npcc_concurrency;
	/* check for max number of messages */
	if (cctx->npcc_msg_sent >= cctx->npcc_msg_rt_max)
		return;
	/* put timestamp and sequence number */
	bs->bs_seq   = ++cctx->npcc_msg_sent;
	bs->bs_cb_nr = 0;
	begin = node_ping_timestamp_put(&ctx->npc_net, buf_index, bs->bs_seq);
	bs->bs_deadline = c2_time_add(begin, ctx->npc_buf_send_timeout);
	/* add message to send queue */
	node_ping_buf_enqueue(ctx, buf_index, C2_NET_QT_MSG_SEND,
			      NULL, ep_index);
	if (bs->bs_errno != 0)
		--cctx->npcc_msg_sent;
}

static void node_ping_client_cb2(struct node_ping_ctx *ctx,
				 size_t buf_index)
{
	struct buf_state *bs = &ctx->npc_buf_state[buf_index];

	C2_PRE(bs->bs_cb_nr == 0 || bs->bs_cb_nr == 1);

	++bs->bs_cb_nr;
	if (bs->bs_cb_nr != 2) {
		node_ping_to_add(ctx, buf_index);
	} else {
		node_ping_to_del(ctx, buf_index);
		/* enqueue recv and send buffers */
		node_ping_buf_enqueue_recv(ctx, bs->bs_index_pair);
		node_ping_client_send(ctx, buf_index);
	}
}

static bool node_ping_client_recv_cb(struct node_ping_ctx *ctx,
				     struct buf_state *bs,
				     size_t buf_index)
{
	struct c2_net_test_timestamp  ts;
	struct buf_state	     *bs_send = NULL;
	ssize_t			      server_index;
	ssize_t			      buf_index_send;
	bool			      decoded;
	bool			      finished;

	C2_PRE(ctx != NULL && ctx->npc_client != NULL);
	C2_PRE(buf_index >= ctx->npc_buf_nr / 2 &&
	       buf_index < ctx->npc_buf_nr);
	C2_PRE(bs != NULL);

	/* check buffer length and offset */
	if (bs->bs_ev.nbe_length != ctx->npc_buf_size ||
	    bs->bs_ev.nbe_offset != 0)
		goto bad_buf;
	/* search for test server index */
	server_index = c2_net_test_network_ep_search(&ctx->npc_net,
					bs->bs_ev.nbe_ep->nep_addr);
	if (server_index == -1)
		goto bad_buf;
	/* decode buffer */
	decoded = node_ping_timestamp_get(&ctx->npc_net, buf_index, &ts);
	if (!decoded)
		goto bad_buf;
	/* check time in received buffer */
	if (bs->bs_time < ts.ntt_time)
		goto bad_buf;
	/* search sequence number */
	buf_index_send = node_ping_client_search_seq(ctx, server_index,
						     ts.ntt_seq);
	if (buf_index_send == -1)
		goto bad_buf;
	bs_send		       = &ctx->npc_buf_state[buf_index_send];
	bs_send->bs_index_pair = buf_index;
	bs->bs_index_pair      = buf_index_send;
	/* successfully received message */
	++ctx->npc_client->npcc_msg_rt;
	finished = ctx->npc_client->npcc_msg_rt >=
		   ctx->npc_client->npcc_msg_rt_max;
	c2_mutex_lock(&ctx->npc_status_data_lock);
	/* update RTT statistics */
	c2_net_test_stats_time_add(&ctx->npc_status_data.ntcsd_rtt,
				   c2_time_sub(bs->bs_time, ts.ntt_time));
	/* set 'client is finished' flag */
	if (equi(finished, !ctx->npc_status_data.ntcsd_finished)) {
		ctx->npc_status_data.ntcsd_finished = true;
		ctx->npc_status_data.ntcsd_time_finish = c2_time_now();
	}
	c2_mutex_unlock(&ctx->npc_status_data_lock);
	goto good_buf;
bad_buf:
	c2_mutex_lock(&ctx->npc_status_data_lock);
	++ctx->npc_status_data.ntcsd_msg_nr_recv.ntmn_bad;
	c2_mutex_unlock(&ctx->npc_status_data_lock);
good_buf:
	/* enqueue recv buffer */
	if (bs_send == NULL) {
		node_ping_buf_enqueue_recv(ctx, buf_index);
	}
	return bs_send != NULL;
}

static void node_ping_msg_cb(struct c2_net_test_network_ctx *net_ctx,
			     uint32_t buf_index,
			     enum c2_net_queue_type q,
			     const struct c2_net_buffer_event *ev)
{
	struct node_ping_ctx *ctx;
	struct buf_state     *bs;
	c2_time_t	      now = c2_time_now();

	C2_PRE(q == C2_NET_QT_MSG_RECV || q == C2_NET_QT_MSG_SEND);

	ctx = node_ping_ctx_from_net_ctx(net_ctx);
	bs = &ctx->npc_buf_state[buf_index];

	LOGD("%s,      role = %d, buf_index = %u, nbe_status = %d, q = %d",
	     __FUNCTION__, ctx->npc_node_role, buf_index, ev->nbe_status, q);
	LOGD(", ev->nbe_length = %lu", ev->nbe_length);

	if (q == C2_NET_QT_MSG_RECV && ev->nbe_status == 0)
		LOGD(", ev->nbe_ep->nep_addr = %s", ev->nbe_ep->nep_addr);

	LOGD("\n");

	if (ev->nbe_status == -ECANCELED) {
		c2_semaphore_up(&ctx->npc_buf_q_sem);
		return;
	}

	/* save buffer event */
	bs->bs_ev = *ev;
	/* save endpoint from successfully received buffer */
	if (ev->nbe_status == 0 &&
	    ev->nbe_buffer->nb_qtype == C2_NET_QT_MSG_RECV)
		c2_net_end_point_get(ev->nbe_ep);
	bs->bs_time = now;
	bs->bs_q    = ev->nbe_buffer->nb_qtype;

	c2_net_test_ringbuf_push(&ctx->npc_buf_rb, buf_index);
	c2_semaphore_up(&ctx->npc_buf_q_sem);
	c2_semaphore_up(&ctx->npc_buf_rb_sem);
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

static void node_ping_to_check(struct node_ping_ctx *ctx)
{
	struct buf_state *bs;
	c2_time_t	  now = c2_time_now();
	ssize_t		  buf_index;

	while ((buf_index = node_ping_to_peek(ctx)) != -1) {
		bs = &ctx->npc_buf_state[buf_index];
		if (bs->bs_deadline > now)
			break;
		/* message timed out */
		node_ping_to_del(ctx, buf_index);
		++ctx->npc_client->npcc_msg_rt;
		node_ping_client_send(ctx, buf_index);
	}
}

static void node_ping_client_handle(struct node_ping_ctx *ctx,
				    struct buf_state *bs,
				    size_t buf_index)
{
	bool good_buf;

	C2_PRE(ctx != NULL);
	C2_PRE(bs != NULL);

	if (bs->bs_q == C2_NET_QT_MSG_SEND) {
		if (bs->bs_errno != 0 || bs->bs_ev.nbe_status != 0) {
			/* try to send again */
			node_ping_client_send(ctx, buf_index);
		} else {
			node_ping_client_cb2(ctx, buf_index);
		}
	} else {
		if (bs->bs_errno != 0 || bs->bs_ev.nbe_status != 0) {
			/* try to receive again */
			node_ping_buf_enqueue_recv(ctx, buf_index);
		} else {
			/* buffer was successfully received from test server */
			good_buf = node_ping_client_recv_cb(ctx, bs, buf_index);
			if (good_buf)
				node_ping_client_cb2(ctx, bs->bs_index_pair);
		}
	}
}

static void node_ping_server_handle(struct node_ping_ctx *ctx,
				    struct buf_state *bs,
				    size_t buf_index)
{
	if (bs->bs_q == C2_NET_QT_MSG_RECV && bs->bs_errno == 0 &&
	    bs->bs_ev.nbe_status == 0) {
		/* send back to test client */
		node_ping_buf_enqueue(ctx, buf_index, C2_NET_QT_MSG_SEND,
				      bs->bs_ev.nbe_ep, 0);
	} else {
		/* add to recv queue */
		node_ping_buf_enqueue_recv(ctx, buf_index);
	}
}

static void node_ping_worker(struct node_ping_ctx *ctx)
{
	struct c2_net_test_msg_nr *msg_nr;
	struct buf_state	  *bs;
	size_t			  buf_index;
	bool			  failed;
	size_t			  i;
	c2_time_t		  to_check_interval;
	c2_time_t		  deadline;
	struct c2_net_end_point	 *ep;
	bool			  rb_is_empty;

	C2_PRE(ctx != NULL);

	c2_time_set(&to_check_interval, TO_CHECK_INTERVAL / 1000,
					TO_CHECK_INTERVAL * 1000000);
	while (1) {
		/* get buffer index from ringbuf */
		deadline = c2_time_add(c2_time_now(), to_check_interval);
		rb_is_empty = !c2_semaphore_timeddown(&ctx->npc_buf_rb_sem,
						      deadline);
		/* check timeout list */
		if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT)
			node_ping_to_check(ctx);
		if (rb_is_empty)
			continue;
		if (ctx->npc_buf_rb_done)
			break;
		buf_index = c2_net_test_ringbuf_pop(&ctx->npc_buf_rb);
		bs = &ctx->npc_buf_state[buf_index];
		LOGD("POP from ringbuf: %lu, role = %d\n",
		     buf_index, ctx->npc_node_role);
		/* update total/failed stats */
		failed = bs->bs_errno != 0 || bs->bs_ev.nbe_status != 0;
		msg_nr = bs->bs_q == C2_NET_QT_MSG_RECV ?
			 &ctx->npc_status_data.ntcsd_msg_nr_recv :
			 &ctx->npc_status_data.ntcsd_msg_nr_send;
		c2_mutex_lock(&ctx->npc_status_data_lock);
		++msg_nr->ntmn_total;
		msg_nr->ntmn_failed += failed;
		c2_mutex_unlock(&ctx->npc_status_data_lock);
		ep = bs->bs_errno == 0 && bs->bs_ev.nbe_status == 0 &&
		     bs->bs_q == C2_NET_QT_MSG_RECV ? bs->bs_ev.nbe_ep : NULL;
		/* handle buffer */
		if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT)
			node_ping_client_handle(ctx, bs, buf_index);
		else
			node_ping_server_handle(ctx, bs, buf_index);
		if (ep != NULL)
			c2_net_end_point_put(ep);
	}
	/* dequeue all buffers */
	for (i = 0; i < ctx->npc_buf_nr; ++i) {
		c2_net_test_network_buffer_dequeue(&ctx->npc_net,
						   C2_NET_TEST_BUF_PING, i);
	}
	/* wait for buffer callbacks */
	for (i = 0; i < ctx->npc_buf_nr; ++i)
		c2_semaphore_down(&ctx->npc_buf_q_sem);
	/* clear ringbuf, put() every saved endpoint */
	/*
	 * use !c2_net_test_ringbuf_is_empty(&ctx->npc_buf_rb) instead of
	 * c2_semaphore_trydown(&ctx->npc_buf_rb_sem) because
	 * ctx->npc_buf_rb_sem may not be up()'ed in buffer callback.
	 */
	while (!c2_net_test_ringbuf_is_empty(&ctx->npc_buf_rb)) {
		buf_index = c2_net_test_ringbuf_pop(&ctx->npc_buf_rb);
		bs = &ctx->npc_buf_state[buf_index];
		if (bs->bs_q == C2_NET_QT_MSG_RECV &&
		    bs->bs_errno == 0 && bs->bs_ev.nbe_status == 0)
			c2_net_end_point_put(bs->bs_ev.nbe_ep);
	}
}

static void node_ping_rb_fill(struct node_ping_ctx *ctx)
{
	size_t i;
	size_t half_buf = ctx->npc_buf_nr / 2;

	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT) {
		C2_ASSERT(ctx->npc_buf_nr % 2 == 0);
		/* add recv buffers */
		for (i = 0; i < half_buf; ++i)
			node_ping_buf_enqueue_recv(ctx, half_buf + i);
		/* add send buffers */
		for (i = 0; i < half_buf; ++i)
			node_ping_client_send(ctx, i);
	} else {
		for (i = 0; i < ctx->npc_buf_nr; ++i)
			node_ping_buf_enqueue_recv(ctx, i);
	}
}

static int node_ping_test_init_fini(struct node_ping_ctx *ctx,
				    const struct c2_net_test_cmd *cmd)
{
	struct c2_net_test_network_timeouts  timeouts;
	int				     rc;
	int				     i;
	char				    *ep_addr;

	if (cmd == NULL) {
		rc = 0;
		if (ctx->npc_test_initialized)
			goto fini;
		else
			goto exit;
	}

	rc = c2_semaphore_init(&ctx->npc_buf_q_sem, ctx->npc_buf_nr);
	if (rc != 0)
		goto exit;
	rc = c2_semaphore_init(&ctx->npc_buf_rb_sem, 0);
	if (rc != 0)
		goto free_buf_q_sem;
	rc = c2_net_test_ringbuf_init(&ctx->npc_buf_rb, ctx->npc_buf_nr);
	if (rc != 0)
		goto free_buf_rb_sem;
	C2_ALLOC_ARR(ctx->npc_buf_state, ctx->npc_buf_nr);
	if (ctx->npc_buf_state == NULL)
		goto free_buf_rb;

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
		goto free_buf_state;
	/* add test node endpoints to the network context endpoint list */
	for (i = 0; i < cmd->ntc_init.ntci_ep.ntsl_nr; ++i) {
		ep_addr = cmd->ntc_init.ntci_ep.ntsl_list[i];
		rc = c2_net_test_network_ep_add(&ctx->npc_net, ep_addr);
		if (rc < 0)
			goto fini;
	}
	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT) {
		buf_state_tlist_init(&ctx->npc_client->npcc_to);
		for (i = 0; i < ctx->npc_buf_nr; ++i)
			buf_state_tlink_init(&ctx->npc_buf_state[i]);
	}
	ctx->npc_test_initialized = true;
	rc = 0;
	goto exit;
fini:
	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT) {
		for (i = 0; i < ctx->npc_buf_nr; ++i)
			buf_state_tlink_fini(&ctx->npc_buf_state[i]);
		buf_state_tlist_fini(&ctx->npc_client->npcc_to);
	}
	c2_net_test_network_ctx_fini(&ctx->npc_net);
free_buf_state:
	c2_free(ctx->npc_buf_state);
free_buf_rb:
	c2_net_test_ringbuf_fini(&ctx->npc_buf_rb);
free_buf_rb_sem:
	c2_semaphore_fini(&ctx->npc_buf_rb_sem);
free_buf_q_sem:
	c2_semaphore_fini(&ctx->npc_buf_q_sem);
exit:
	return rc;
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
		node_ping_test_init_fini(ctx, NULL);
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
	bool				    finished;

	C2_PRE(ctx != NULL);
	sd = &ctx->npc_status_data;

	/* update MPS stats */
	c2_mutex_lock(&ctx->npc_status_data_lock);
	msg_send = sd->ntcsd_msg_nr_send;
	msg_recv = sd->ntcsd_msg_nr_recv;
	now	 = c2_time_now();
	finished = sd->ntcsd_finished;
	c2_mutex_unlock(&ctx->npc_status_data_lock);

	if (!finished) {
		/*
		 * MPS stats can be updated without lock because
		 * they are used in node_ping_step() and
		 * node_ping_cmd_status(), which are serialized.
		 */
		c2_net_test_mps_add(&sd->ntcsd_mps_send,
				    msg_send.ntmn_total, now);
		c2_net_test_mps_add(&sd->ntcsd_mps_recv,
				    msg_recv.ntmn_total, now);
	}

	return 0;
}

static int node_ping_cmd_init(void *ctx_,
			      const struct c2_net_test_cmd *cmd,
			      struct c2_net_test_cmd *reply)
{
	struct node_ping_ctx *ctx = ctx_;
	int		      rc;

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
	if (ctx->npc_test_initialized) {
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

	if (ctx->npc_node_role == C2_NET_TEST_ROLE_CLIENT) {
		C2_SET0(ctx->npc_client);
		ctx->npc_client->npcc_msg_rt_max = cmd->ntc_init.ntci_msg_nr;
		ctx->npc_client->npcc_concurrency =
			cmd->ntc_init.ntci_concurrency;
	}

	/* do sanity check */
	rc = 0;
	if (ctx->npc_buf_size < 1 || ctx->npc_buf_nr < 1 ||
	    equi(ctx->npc_client == NULL, ctx->npc_server == NULL))
		rc = -EINVAL;
	/* init node_ping_ctx fields */
	if (rc == 0)
		rc = node_ping_test_init_fini(ctx, cmd);
	if (rc != 0) {
		/* change service state */
		c2_net_test_service_state_change(ctx->npc_svc,
						 C2_NET_TEST_SERVICE_FAILED);
	}
reply:
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
	c2_time_t			    _1s = C2_MKTIME(1, 0);

	C2_PRE(ctx != NULL);
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);

	LOGD("%s\n", __FUNCTION__);

	sd = &ctx->npc_status_data;
	C2_SET0(sd);

	/* fill test start time */
	sd->ntcsd_time_start = c2_time_now();
	/* initialize stats */
	c2_net_test_mps_init(&sd->ntcsd_mps_send, 0, sd->ntcsd_time_start, _1s);
	c2_net_test_mps_init(&sd->ntcsd_mps_recv, 0, sd->ntcsd_time_start, _1s);
	c2_net_test_stats_reset(&sd->ntcsd_rtt);
	/* add buffer indexes to ringbuf */
	node_ping_rb_fill(ctx);
	/* start test */
	ctx->npc_buf_rb_done = false;
	rc = C2_THREAD_INIT(&ctx->npc_thread, struct node_ping_ctx *, NULL,
			    &node_ping_worker, ctx,
			    "net-test-worker#%s",
			    ctx->npc_net.ntc_tm->ntm_ep->nep_addr);
	if (rc != 0) {
		/* change service state */
		c2_net_test_service_state_change(ctx->npc_svc,
						 C2_NET_TEST_SERVICE_FAILED);
	}
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
	int		      rc;

	C2_PRE(ctx != NULL);
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);

	LOGD("%s\n", __FUNCTION__);

	/* stop worker thread */
	ctx->npc_buf_rb_done = true;
	c2_semaphore_up(&ctx->npc_buf_rb_sem);
	rc = c2_thread_join(&ctx->npc_thread);
	C2_ASSERT(rc == 0);
	c2_thread_fini(&ctx->npc_thread);
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
		LOGD("ctx->npc_client->npcc_msg_rt = %lu\n",
		     ctx->npc_client->npcc_msg_rt);
		LOGD("ctx->npc_client->npcc_msg_sent = %lu\n",
		     ctx->npc_client->npcc_msg_sent);
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
