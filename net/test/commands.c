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
 * Original creation date: 05/05/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/cdefs.h"		/* container_of */
#include "lib/types.h"		/* c2_bcount_t */
#include "lib/misc.h"		/* C2_SET0 */
#include "lib/memory.h"		/* C2_ALLOC_ARR */
#include "lib/errno.h"		/* ENOMEM */

#include "net/test/serialize.h"	/* c2_net_test_serialize */
#include "net/test/str.h"	/* c2_net_test_str */

#include "net/test/commands.h"

/**
   @defgroup NetTestCommandsInternals Commands
   @ingroup NetTestInternals

   @see
   @ref net-test

   @{
 */

/* c2_net_test_cmd_descr */
TYPE_DESCR(c2_net_test_cmd) = {
	FIELD_DESCR(struct c2_net_test_cmd, ntc_type),
};

/* c2_net_test_cmd_done_descr */
TYPE_DESCR(c2_net_test_cmd_done) = {
	FIELD_DESCR(struct c2_net_test_cmd_done, ntcd_errno),
};

/* c2_net_test_cmd_init_descr */
TYPE_DESCR(c2_net_test_cmd_init) = {
	FIELD_DESCR(struct c2_net_test_cmd_init, ntci_role),
	FIELD_DESCR(struct c2_net_test_cmd_init, ntci_type),
	FIELD_DESCR(struct c2_net_test_cmd_init, ntci_msg_nr),
	FIELD_DESCR(struct c2_net_test_cmd_init, ntci_msg_size),
	FIELD_DESCR(struct c2_net_test_cmd_init, ntci_concurrency),
	FIELD_DESCR(struct c2_net_test_cmd_init, ntci_buf_send_timeout),
};

/* c2_net_test_cmd_status_data_descr */
TYPE_DESCR(c2_net_test_cmd_status_data) = {
	FIELD_DESCR(struct c2_net_test_cmd_status_data, ntcsd_msg_sent),
	FIELD_DESCR(struct c2_net_test_cmd_status_data, ntcsd_msg_rcvd),
	FIELD_DESCR(struct c2_net_test_cmd_status_data, ntcsd_msg_send_failed),
	FIELD_DESCR(struct c2_net_test_cmd_status_data, ntcsd_msg_recv_failed),
	FIELD_DESCR(struct c2_net_test_cmd_status_data, ntcsd_bytes_sent),
	FIELD_DESCR(struct c2_net_test_cmd_status_data, ntcsd_bytes_rcvd),
	FIELD_DESCR(struct c2_net_test_cmd_status_data, ntcsd_time_start),
	FIELD_DESCR(struct c2_net_test_cmd_status_data, ntcsd_time_now),
};

static c2_bcount_t
cmd_status_data_serialize(enum c2_net_test_serialize_op op,
			  struct c2_net_test_cmd_status_data *status_data,
			  struct c2_bufvec *bv,
			  c2_bcount_t offset)
{
	struct c2_net_test_stats *stats;
	c2_bcount_t		  len;
	c2_bcount_t		  len_total;
	int			  i;

	len = c2_net_test_serialize(op, status_data,
			USE_TYPE_DESCR(c2_net_test_cmd_status_data),
			bv, offset);
	if ((len_total = len) == 0)
		return len;

	for (i = 0; i < 3; ++i) {
		stats = status_data == NULL ? NULL :
			i == 0 ? &status_data->ntcsd_bandwidth_1s_send :
			i == 1 ? &status_data->ntcsd_bandwidth_1s_recv :
			i == 2 ? &status_data->ntcsd_rtt : NULL;

		len = c2_net_test_stats_serialize(op, stats, bv,
						  offset + len_total);
		if (len == 0)
			break;
		len_total += len;
	}
	return len == 0 ? 0 : len_total;
}

/**
   Serialize/deserialize c2_net_test_cmd to/from c2_net_buffer
   @param op operation. Can be C2_NET_TEST_SERIALIZE or C2_NET_TEST_DESERIALIZE
   @param cmd command for transforming.
   @param buf can be NULL if op == C2_NET_TEST_SERIALIZE,
	      in this case offset is ignored but length is set.
   @param offset start of serialized data in buf.
   @param length if isn't NULL then store length of serialized command here.
 */
static int cmd_serialize(enum c2_net_test_serialize_op op,
			 struct c2_net_test_cmd *cmd,
			 struct c2_net_buffer *buf,
			 c2_bcount_t offset,
			 c2_bcount_t *length)
{
	struct c2_bufvec *bv = buf == NULL ? NULL : &buf->nb_buffer;
	c2_bcount_t	  len;
	c2_bcount_t	  len_total;

	C2_PRE(cmd != NULL);
	len = len_total = c2_net_test_serialize(op, cmd,
				  USE_TYPE_DESCR(c2_net_test_cmd), bv, offset);
	if (len_total == 0)
		return -EINVAL;

	switch (cmd->ntc_type) {
	case C2_NET_TEST_CMD_INIT:
		len = c2_net_test_serialize(op, &cmd->ntc_init,
					USE_TYPE_DESCR(c2_net_test_cmd_init),
					bv, offset + len_total);
		if (len == 0)
			break;
		len_total += len;

		len = c2_net_test_str_serialize(op, &cmd->ntc_init.ntci_tm_ep,
						bv, offset + len_total);
		if (len == 0)
			break;
		len_total += len;


		len = c2_net_test_slist_serialize(op, &cmd->ntc_init.ntci_ep,
						  bv, offset + len_total);
		break;
	case C2_NET_TEST_CMD_START:
	case C2_NET_TEST_CMD_STOP:
	case C2_NET_TEST_CMD_STATUS:
		break;
	case C2_NET_TEST_CMD_STATUS_DATA:
		len = cmd_status_data_serialize(op, &cmd->ntc_status_data,
						bv, offset + len_total);
		break;
	case C2_NET_TEST_CMD_INIT_DONE:
	case C2_NET_TEST_CMD_START_DONE:
	case C2_NET_TEST_CMD_STOP_DONE:
		len = c2_net_test_serialize(op, &cmd->ntc_done,
					    USE_TYPE_DESCR(c2_net_test_cmd_done),
					    bv, offset + len_total);
		break;
	default:
		return -ENOSYS;
	};

	return len == 0 ? -EINVAL : 0;
}

/**
   Free c2_net_test_cmd after succesful
   cmd_serialize(C2_NET_TEST_DESERIALIZE, ...).
 */
static void cmd_free(struct c2_net_test_cmd *cmd)
{
	C2_PRE(cmd != NULL);

	if (cmd->ntc_type == C2_NET_TEST_CMD_INIT) {
		c2_net_test_str_fini  (&cmd->ntc_init.ntci_tm_ep);
		c2_net_test_slist_fini(&cmd->ntc_init.ntci_ep);
	}
}

static struct c2_net_test_cmd_ctx *
cmd_ctx_extract(struct c2_net_test_network_ctx *net_ctx)
{
	C2_PRE(net_ctx != NULL);

	return container_of(net_ctx, struct c2_net_test_cmd_ctx, ntcc_net);
}

/**
   Search for ep_addr in c2_net_test_cmd_ctx.ntcc_net->ntc_ep
   This function have time complexity
   of O(number of endpoints in the network context).
   @return >= 0 endpoint index
   @return -1 endpoint not found
 */
static ssize_t ep_search(struct c2_net_test_cmd_ctx *ctx, const char *ep_addr)
{
	struct c2_net_end_point **ep_arr = ctx->ntcc_net.ntc_ep;
	size_t			  ep_nr = ctx->ntcc_net.ntc_ep_nr;
	size_t			  i;
	size_t			  addr_len = strlen(ep_addr) + 1;

	for (i = 0; i < ep_nr; ++i)
		if (strncmp(ep_addr, ep_arr[i]->nep_addr, addr_len) == 0)
			return i;
	return -1;
}

static void commands_tm_event_cb(const struct c2_net_tm_event *ev)
{
	/* nothing here for now */
}

static void commands_cb_msg_recv(struct c2_net_test_network_ctx *net_ctx,
				 const uint32_t buf_index,
				 enum c2_net_queue_type q,
				 const struct c2_net_buffer_event *ev)
{
	struct c2_net_test_cmd_ctx *ctx = cmd_ctx_extract(net_ctx);

	C2_PRE(c2_net_test_commands_invariant(ctx));
	C2_PRE(q == C2_NET_QT_MSG_RECV);

	/* save endpoint and buffer status */
	if (ev->nbe_ep != NULL)
		c2_net_end_point_get(ev->nbe_ep);
	ctx->ntcc_buf_status[buf_index].ntcbs_ep = ev->nbe_ep;
	ctx->ntcc_buf_status[buf_index].ntcbs_buf_status = ev->nbe_status;

	/* put buffer to ringbuf */
	c2_net_test_ringbuf_push(&ctx->ntcc_rb, buf_index);

	/* c2_net_test_commands_recv() will down this semaphore */
	c2_semaphore_up(&ctx->ntcc_sem_recv);
}

static void commands_cb_msg_send(struct c2_net_test_network_ctx *net_ctx,
				 const uint32_t buf_index,
				 enum c2_net_queue_type q,
				 const struct c2_net_buffer_event *ev)
{
	struct c2_net_test_cmd_ctx *ctx = cmd_ctx_extract(net_ctx);

	C2_PRE(c2_net_test_commands_invariant(ctx));
	C2_PRE(q == C2_NET_QT_MSG_SEND);

	/* invoke 'message sent' callback if it is present */
	if (ctx->ntcc_send_cb != NULL)
		ctx->ntcc_send_cb(ctx, buf_index, ev->nbe_status);

	c2_semaphore_up(&ctx->ntcc_sem_send);
}

static void commands_cb_impossible(struct c2_net_test_network_ctx *ctx,
				   const uint32_t buf_index,
				   enum c2_net_queue_type q,
				   const struct c2_net_buffer_event *ev)
{

	C2_IMPOSSIBLE("commands bulk buffer callback is impossible");
}

static const struct c2_net_tm_callbacks c2_net_test_commands_tm_cb = {
	.ntc_event_cb = commands_tm_event_cb
};

static const struct c2_net_test_network_buffer_callbacks commands_buffer_cb = {
	.ntnbc_cb = {
		[C2_NET_QT_MSG_RECV]		= commands_cb_msg_recv,
		[C2_NET_QT_MSG_SEND]		= commands_cb_msg_send,
		[C2_NET_QT_PASSIVE_BULK_RECV]	= commands_cb_impossible,
		[C2_NET_QT_PASSIVE_BULK_SEND]	= commands_cb_impossible,
		[C2_NET_QT_ACTIVE_BULK_RECV]	= commands_cb_impossible,
		[C2_NET_QT_ACTIVE_BULK_SEND]	= commands_cb_impossible,
	}
};

static int commands_recv_enqueue(struct c2_net_test_cmd_ctx *ctx,
				 size_t buf_index)
{
	int rc;

	ctx->ntcc_buf_status[buf_index].ntcbs_in_recv_queue = true;
	rc = c2_net_test_network_msg_recv(&ctx->ntcc_net, buf_index);
	if (rc != 0)
		ctx->ntcc_buf_status[buf_index].ntcbs_in_recv_queue = false;

	return rc;
}

static void commands_recv_dequeue(struct c2_net_test_cmd_ctx *ctx,
				  size_t buf_index)
{
	c2_net_test_network_buffer_dequeue(&ctx->ntcc_net, C2_NET_TEST_BUF_PING,
					   buf_index);
}

static void commands_recv_ep_put(struct c2_net_test_cmd_ctx *ctx,
				 size_t buf_index)
{
	if (ctx->ntcc_buf_status[buf_index].ntcbs_ep != NULL)
		c2_net_end_point_put(ctx->ntcc_buf_status[buf_index].ntcbs_ep);
}

static bool is_buf_in_recv_q(struct c2_net_test_cmd_ctx *ctx,
			  size_t buf_index)
{
	C2_PRE(buf_index < ctx->ntcc_ep_nr * 2);

	return ctx->ntcc_buf_status[buf_index].ntcbs_in_recv_queue;
}

static void commands_recv_dequeue_nr(struct c2_net_test_cmd_ctx *ctx,
				     size_t nr)
{
	size_t i;

	/* remove recv buffers from queue */
	for (i = 0; i < nr; ++i)
		if (is_buf_in_recv_q(ctx, ctx->ntcc_ep_nr + i))
			commands_recv_dequeue(ctx, ctx->ntcc_ep_nr + i);
	/* wait until callbacks executed */
	for (i = 0; i < nr; ++i)
		if (is_buf_in_recv_q(ctx, ctx->ntcc_ep_nr + i))
			c2_semaphore_down(&ctx->ntcc_sem_recv);
	/* release endpoints */
	for (i = 0; i < nr; ++i)
		if (is_buf_in_recv_q(ctx, ctx->ntcc_ep_nr + i))
			commands_recv_ep_put(ctx, ctx->ntcc_ep_nr + i);
}

static int commands_initfini(struct c2_net_test_cmd_ctx *ctx,
			     const char *cmd_ep,
			     c2_time_t send_timeout,
			     c2_net_test_commands_send_cb_t send_cb,
			     struct c2_net_test_slist *ep_list,
			     bool init)
{
	struct c2_net_test_network_timeouts timeouts;
	int				    i;
	int				    rc = -EEXIST;

	C2_PRE(ctx != NULL);
	if (!init)
		goto fini;

	C2_PRE(ep_list->ntsl_nr > 0);
	C2_SET0(ctx);

	if (!c2_net_test_slist_unique(ep_list))
		goto fail;

	timeouts = c2_net_test_network_timeouts_never();
	timeouts.ntnt_timeout[C2_NET_QT_MSG_SEND] = send_timeout;

	ctx->ntcc_ep_nr   = ep_list->ntsl_nr;
	ctx->ntcc_send_cb = send_cb;

	c2_mutex_init(&ctx->ntcc_send_mutex);
	rc = c2_semaphore_init(&ctx->ntcc_sem_send, 0);
	if (rc != 0)
		goto fail;
	rc = c2_semaphore_init(&ctx->ntcc_sem_recv, 0);
	if (rc != 0)
		goto free_sem_send;

	rc = c2_net_test_ringbuf_init(&ctx->ntcc_rb, ctx->ntcc_ep_nr * 2);
	if (rc != 0)
		goto free_sem_recv;

	C2_ALLOC_ARR(ctx->ntcc_buf_status, ctx->ntcc_ep_nr * 2);
	if (ctx->ntcc_buf_status == NULL)
		goto free_rb;

	rc = c2_net_test_network_ctx_init(&ctx->ntcc_net, cmd_ep,
					  &c2_net_test_commands_tm_cb,
					  &commands_buffer_cb,
					  C2_NET_TEST_CMD_SIZE_MAX,
					  2 * ctx->ntcc_ep_nr,
					  0, 0,
					  ep_list->ntsl_nr,
					  &timeouts);
	if (rc != 0)
		goto free_buf_status;

	for (i = 0; i < ep_list->ntsl_nr; ++i)
		if ((rc = c2_net_test_network_ep_add(&ctx->ntcc_net,
						ep_list->ntsl_list[i])) < 0)
			goto free_net_ctx;
	for (i = 0; i < ctx->ntcc_ep_nr; ++i)
		if ((rc = commands_recv_enqueue(ctx,
						ctx->ntcc_ep_nr + i)) < 0) {
			commands_recv_dequeue_nr(ctx, i);
			goto free_net_ctx;
		}

	C2_POST(c2_net_test_commands_invariant(ctx));
	rc = 0;
	goto success;

    fini:
	C2_PRE(c2_net_test_commands_invariant(ctx));
	c2_net_test_commands_send_wait_all(ctx);
	commands_recv_dequeue_nr(ctx, ctx->ntcc_ep_nr);
    free_net_ctx:
	c2_net_test_network_ctx_fini(&ctx->ntcc_net);
    free_buf_status:
	c2_free(ctx->ntcc_buf_status);
    free_rb:
	c2_net_test_ringbuf_fini(&ctx->ntcc_rb);
    free_sem_recv:
	c2_semaphore_fini(&ctx->ntcc_sem_recv);
    free_sem_send:
	c2_semaphore_fini(&ctx->ntcc_sem_send);
    fail:
	c2_mutex_fini(&ctx->ntcc_send_mutex);
    success:
	return rc;
}

int c2_net_test_commands_init(struct c2_net_test_cmd_ctx *ctx,
			      const char *cmd_ep,
			      c2_time_t send_timeout,
			      c2_net_test_commands_send_cb_t send_cb,
			      struct c2_net_test_slist *ep_list)
{
	return commands_initfini(ctx, cmd_ep, send_timeout, send_cb, ep_list,
				 true);
}

void c2_net_test_commands_fini(struct c2_net_test_cmd_ctx *ctx)
{
	commands_initfini(ctx, NULL, C2_TIME_NEVER, NULL, NULL, false);
	C2_SET0(ctx);
}

int c2_net_test_commands_send(struct c2_net_test_cmd_ctx *ctx,
			      struct c2_net_test_cmd *cmd)
{
	struct c2_net_buffer *buf;
	int		      rc;
	size_t		      buf_index;

	C2_PRE(c2_net_test_commands_invariant(ctx));
	C2_PRE(cmd != NULL);

	buf_index = cmd->ntc_ep_index;
	buf = c2_net_test_network_buf(&ctx->ntcc_net, C2_NET_TEST_BUF_PING,
				      buf_index);

	rc = cmd_serialize(C2_NET_TEST_SERIALIZE, cmd, buf, 0, NULL);
	if (rc == 0)
		rc = c2_net_test_network_msg_send(&ctx->ntcc_net, buf_index,
						  cmd->ntc_ep_index);

	if (rc == 0) {
		c2_mutex_lock(&ctx->ntcc_send_mutex);
		ctx->ntcc_send_nr++;
		c2_mutex_unlock(&ctx->ntcc_send_mutex);
	}

	return rc;
}

void c2_net_test_commands_send_wait_all(struct c2_net_test_cmd_ctx *ctx)
{
	int64_t nr;
	int64_t i;

	C2_PRE(c2_net_test_commands_invariant(ctx));

	c2_mutex_lock(&ctx->ntcc_send_mutex);
	nr = ctx->ntcc_send_nr;
	ctx->ntcc_send_nr = 0;
	c2_mutex_unlock(&ctx->ntcc_send_mutex);

	for (i = 0; i < nr; ++i)
		c2_semaphore_down(&ctx->ntcc_sem_send);
}

int c2_net_test_commands_recv(struct c2_net_test_cmd_ctx *ctx,
			      struct c2_net_test_cmd *cmd,
			      c2_time_t deadline)
{
	struct c2_net_buffer	*buf;
	struct c2_net_end_point *ep;
	bool			 rc_bool;
	size_t			 buf_index;
	int			 rc;

	C2_PRE(c2_net_test_commands_invariant(ctx));
	C2_PRE(cmd != NULL);

	/* wait for received buffer */
	rc_bool = c2_semaphore_timeddown(&ctx->ntcc_sem_recv, deadline);
	/* buffer wasn't received before deadline */
	if (!rc_bool)
		return -ETIMEDOUT;

	/* get buffer */
	buf_index = c2_net_test_ringbuf_pop(&ctx->ntcc_rb);
	C2_ASSERT(is_buf_in_recv_q(ctx, buf_index));
	buf = c2_net_test_network_buf(&ctx->ntcc_net, C2_NET_TEST_BUF_PING,
				      buf_index);

	/* deserialize buffer to cmd */
	rc = cmd_serialize(C2_NET_TEST_DESERIALIZE, cmd, buf, 0, NULL);
	if (rc != 0)
		cmd->ntc_type = C2_NET_TEST_CMD_NR;

	/* set c2_net_test_cmd.ntc_ep_index and release endpoint */
	ep = ctx->ntcc_buf_status[buf_index].ntcbs_ep;
	cmd->ntc_ep_index = ep_search(ctx, ep->nep_addr);
	c2_net_end_point_put(ep);

	/* set c2_net_test_cmd.ntc_buf_index */
	cmd->ntc_buf_index = buf_index;

	/* buffer now not in receive queue */
	ctx->ntcc_buf_status[buf_index].ntcbs_in_recv_queue = false;

	return rc;
}

int c2_net_test_commands_recv_enqueue(struct c2_net_test_cmd_ctx *ctx,
				      size_t buf_index)
{
	C2_PRE(c2_net_test_commands_invariant(ctx));

	return commands_recv_enqueue(ctx, buf_index);
}

void c2_net_test_commands_received_free(struct c2_net_test_cmd *cmd)
{
	cmd_free(cmd);
}

bool c2_net_test_commands_invariant(struct c2_net_test_cmd_ctx *ctx)
{

	if (ctx == NULL)
		return false;
	if (ctx->ntcc_ep_nr == 0)
		return false;
	if (ctx->ntcc_ep_nr != ctx->ntcc_net.ntc_ep_nr)
		return false;
	if (ctx->ntcc_ep_nr * 2 != ctx->ntcc_net.ntc_buf_ping_nr)
		return false;
	if (ctx->ntcc_net.ntc_buf_bulk_nr != 0)
		return false;
	return true;
}

/**
   @} end of NetTestCommandsInternals group
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
