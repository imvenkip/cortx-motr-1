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

#include "net/test/ntxcode.h"	/* c2_net_test_xcode */

#include "net/test/commands.h"

/** @todo remove */
#ifndef __KERNEL__
#include <stdio.h>		/* printf */
#endif

#ifndef __KERNEL__
#define LOGD(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOGD(format, ...) do {} while (0)
#endif

/**
   @defgroup NetTestCommandsInternals Colibri Network Bencmark \
				    Commands Internals

   @see
   @ref net-test

   @{
 */

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
   @return UINT32_MAX endpoint not found
 */
static int32_t ep_index(struct c2_net_test_cmd_ctx *ctx, const char *ep_addr)
{
	struct c2_net_end_point **ep_arr = ctx->ntcc_net.ntc_ep;
	uint32_t		  ep_nr = ctx->ntcc_net.ntc_ep_nr;
	int32_t			  i;
	size_t			  addr_len = strlen(ep_addr);

	C2_PRE(ep_nr < UINT32_MAX);
	for (i = 0; i < ep_nr; ++i)
		if (strncmp(ep_addr, ep_arr[i]->nep_addr, addr_len) == 0)
			return i;
	return UINT32_MAX;
}

/* c2_net_test_cmd_descr */
TYPE_DESCR(c2_net_test_cmd) = {
	FIELD_DESCR(struct c2_net_test_cmd, ntc_type),
};

/* c2_net_test_cmd_ack_descr */
TYPE_DESCR(c2_net_test_cmd_ack) = {
	FIELD_DESCR(struct c2_net_test_cmd_ack, ntca_errno),
};

/* c2_net_test_cmd_init_descr */
TYPE_DESCR(c2_net_test_cmd_init) = {
	FIELD_DESCR(struct c2_net_test_cmd_init, ntci_role),
	FIELD_DESCR(struct c2_net_test_cmd_init, ntci_type),
	FIELD_DESCR(struct c2_net_test_cmd_init, ntci_msg_nr),
	FIELD_DESCR(struct c2_net_test_cmd_init, ntci_bulk_size),
	FIELD_DESCR(struct c2_net_test_cmd_init, ntci_concurrency),
};

/* c2_net_test_cmd_stop_descr */
TYPE_DESCR(c2_net_test_cmd_stop) = {
	FIELD_DESCR(struct c2_net_test_cmd_stop, ntcs_cancel),
};

/**
   Encode/decode c2_net_test_cmd to/from c2_net_buffer
   @param op operation. Can be C2_NET_TEST_ENCODE or C2_NET_TEST_DECODE.
   @param cmd command for transforming.
   @param buf can be NULL if op == C2_NET_TEST_ENCODE,
	      in this case offset is ignored but length is set.
   @param offset start of encoded data in buf.
   @param length if isn't NULL then store length of encoded command here.
 */
static int cmd_xcode(enum c2_net_test_xcode_op op,
		     struct c2_net_test_cmd *cmd,
		     struct c2_net_buffer *buf,
		     c2_bcount_t offset,
		     c2_bcount_t *length)
{
	struct c2_bufvec *bv = buf == NULL ? NULL : &buf->nb_buffer;
	c2_bcount_t	  len;
	c2_bcount_t	  len_total;

	C2_PRE(cmd != NULL);
	len_total = c2_net_test_xcode(op, cmd, USE_TYPE_DESCR(c2_net_test_cmd),
				      bv, offset);
	if (len_total == 0)
		return -EINVAL;

	switch (cmd->ntc_type) {
	case C2_NET_TEST_CMD_INIT:
		len = c2_net_test_xcode(op, &cmd->ntc_init,
					USE_TYPE_DESCR(c2_net_test_cmd_init),
					bv, offset + len_total);
		if (len == 0)
			break;
		len_total += len;

		len = c2_net_test_slist_xcode(op, &cmd->ntc_init.ntci_ep,
					      bv, offset + len_total);
		break;
	case C2_NET_TEST_CMD_STOP:
		len = c2_net_test_xcode(op, &cmd->ntc_stop,
					USE_TYPE_DESCR(c2_net_test_cmd_stop),
					bv, offset + len_total);
		break;
	case C2_NET_TEST_CMD_START_ACK:
	case C2_NET_TEST_CMD_STOP_ACK:
	case C2_NET_TEST_CMD_FINISHED_ACK:
		len = c2_net_test_xcode(op, &cmd->ntc_ack,
					USE_TYPE_DESCR(c2_net_test_cmd_ack),
					bv, offset + len_total);
		break;
	default:
		return -ENOSYS;
	};

	if (len == 0)
		return -EINVAL;
	len_total += len;
	if (length != NULL)
		*length = len_total;
	return 0;
}

/**
   Get encoded command length.
 */
static c2_bcount_t cmd_length(struct c2_net_test_cmd *cmd)
{
	c2_bcount_t length;

	return cmd_xcode(C2_NET_TEST_ENCODE, cmd, NULL, 0, &length) == 0 ?
	       length : 0;
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
	uint32_t cmd_index;

	C2_PRE(c2_net_test_commands_invariant(ctx));
	C2_PRE(q == C2_NET_QT_MSG_RECV);

	LOGD("cb_msg_recv\n");
	if (ev->nbe_status == 0) {
		/* search for a command index */
		cmd_index = ep_index(ctx, ev->nbe_ep->nep_addr);
		/* message from some other endpoint. just ignore it */
		/* @todo addb? */
		if (cmd_index == UINT32_MAX) {
			c2_net_test_network_msg_recv(&ctx->ntcc_net, buf_index);
			return;
		}
		/* save buffer index and status */
		ctx->ntcc_cmd[cmd_index].ntc_buf_index  = buf_index;
		ctx->ntcc_cmd[cmd_index].ntc_buf_status = ev->nbe_status;
	}
	/* receiver will down this semaphore to wait for all callbacks */
	c2_semaphore_up(&ctx->ntcc_sem);
}

static void commands_cb_msg_send(struct c2_net_test_network_ctx *net_ctx,
				 const uint32_t buf_index,
				 enum c2_net_queue_type q,
				 const struct c2_net_buffer_event *ev)
{
	struct c2_net_test_cmd_ctx *ctx = cmd_ctx_extract(net_ctx);

	C2_PRE(c2_net_test_commands_invariant(ctx));
	C2_PRE(q == C2_NET_QT_MSG_SEND);

	LOGD("cb_msg_send\n");
	/* a command index is equal to the buffer index buf_index */
	/* save buffer status */
	ctx->ntcc_cmd[buf_index].ntc_buf_status = ev->nbe_status;
	/* sender will down this semaphore to wait for all callbacks */
	c2_semaphore_up(&ctx->ntcc_sem);
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

int c2_net_test_commands_init(struct c2_net_test_cmd_ctx *ctx,
			     char *cmd_ep,
			     c2_time_t timeout_send,
			     c2_time_t timeout_wait,
			     struct c2_net_test_slist *ep_list)
{
	int				    rc;
	int				    i;
	struct c2_net_test_network_timeouts timeouts;

	C2_PRE(ctx != NULL);
	C2_PRE(ep_list->ntsl_nr > 0);
	C2_SET0(ctx);

	if (!c2_net_test_slist_unique(ep_list))
		return -EEXIST;

	timeouts = c2_net_test_network_timeouts_never();
	timeouts.ntnt_timeout[C2_NET_QT_MSG_SEND] = timeout_send;
	timeouts.ntnt_timeout[C2_NET_QT_MSG_RECV] = timeout_wait;

	ctx->ntcc_cmd_nr = ep_list->ntsl_nr;
	C2_ALLOC_ARR(ctx->ntcc_cmd, ctx->ntcc_cmd_nr);
	if (ctx->ntcc_cmd == NULL)
		return -ENOMEM;

	rc = c2_semaphore_init(&ctx->ntcc_sem, 0);
	if (rc != 0)
		goto free_cmd;

	rc = c2_net_test_network_ctx_init(&ctx->ntcc_net, cmd_ep,
					  &c2_net_test_commands_tm_cb,
					  &commands_buffer_cb,
					  C2_NET_TEST_CMD_SIZE_MAX,
					  ctx->ntcc_cmd_nr,
					  0, 0,
					  ep_list->ntsl_nr,
					  &timeouts);
	if (rc != 0)
		goto free_sem;

	for (i = 0; i < ep_list->ntsl_nr; ++i) {
		rc = c2_net_test_network_ep_add(&ctx->ntcc_net,
						ep_list->ntsl_list[i]);
		if (rc < 0)
			goto free_net;
	}

	c2_net_test_commands_reset(ctx);
	C2_POST(c2_net_test_commands_invariant(ctx));
	rc = 0;
	goto success;

    free_net:
	c2_net_test_network_ctx_fini(&ctx->ntcc_net);
    free_sem:
	c2_semaphore_fini(&ctx->ntcc_sem);
    free_cmd:
	c2_free(ctx->ntcc_cmd);
    success:
	return rc;
}

void c2_net_test_commands_fini(struct c2_net_test_cmd_ctx *ctx)
{
	C2_PRE(ctx != NULL);
	C2_PRE(c2_net_test_commands_invariant(ctx));

	c2_net_test_network_ctx_fini(&ctx->ntcc_net);
	c2_semaphore_fini(&ctx->ntcc_sem);
	c2_free(ctx->ntcc_cmd);
}

static bool cmd_success(struct c2_net_test_cmd *cmd)
{
	return !cmd->ntc_disabled && cmd->ntc_errno == 0 &&
		cmd->ntc_buf_status == 0;
}

static void commands_disable_succesful(struct c2_net_test_cmd_ctx *ctx)
{
	uint32_t		i;
	struct c2_net_test_cmd *cmd_i;

	for (i = 0; i < ctx->ntcc_cmd_nr; ++i) {
		cmd_i = &ctx->ntcc_cmd[i];
		cmd_i->ntc_disabled = cmd_i->ntc_disabled || cmd_success(cmd_i);
	}
}

static uint32_t commands_network_operation(struct c2_net_test_cmd_ctx *ctx,
					   bool network_send)
{
	uint32_t		i;
	uint32_t		queued_cmd_nr;
	uint32_t		success_cmd_nr;
	struct c2_net_test_cmd *cmd_i;
	int			rc;

	/* add all command buffers to the message send/recv queue */
	queued_cmd_nr = 0;
	for (i = 0; i < ctx->ntcc_cmd_nr; ++i) {
		cmd_i = &ctx->ntcc_cmd[i];

		if (cmd_i->ntc_disabled)
			continue;

		rc = network_send ? cmd_i->ntc_errno : 0;
		rc = rc != 0 ? rc : network_send ?
			 c2_net_test_network_msg_send(&ctx->ntcc_net, i, i) :
			 c2_net_test_network_msg_recv(&ctx->ntcc_net, i);

		queued_cmd_nr += rc == 0;
		cmd_i->ntc_errno = rc;
	}

	/* wait until all callbacks executed */
	for (i = 0; i < queued_cmd_nr; ++i)
		c2_semaphore_down(&ctx->ntcc_sem);

	/* count the number of sent/received commands */
	success_cmd_nr = 0;
	for (i = 0; i < ctx->ntcc_cmd_nr; ++i)
		success_cmd_nr += cmd_success(&ctx->ntcc_cmd[i]);

	return success_cmd_nr;
}

uint32_t c2_net_test_commands_send(struct c2_net_test_cmd_ctx *ctx,
				   struct c2_net_test_cmd *cmd)
{
	uint32_t		i;
	struct c2_net_test_cmd *cmd_i;
	struct c2_net_buffer   *buf;
	uint32_t		result;

	C2_PRE(c2_net_test_commands_invariant(ctx));

	/* encode all commands to buffers */
	for (i = 0; i < ctx->ntcc_cmd_nr; ++i) {
		cmd_i = c2_net_test_command(ctx, i);
		*cmd_i = cmd == NULL ? *cmd_i : *cmd;

		/* skip this command if it is disabled */
		if (cmd_i->ntc_disabled)
			continue;

		/* check command length */
		/** @todo use c2_net_test_network_buf_resize() */
		if (cmd_length(cmd_i) > C2_NET_TEST_CMD_SIZE_MAX)
			return -E2BIG;

		/* encode command to c2_net_buffer */
		buf = c2_net_test_network_buf(&ctx->ntcc_net,
					      C2_NET_TEST_BUF_PING, i);
		C2_ASSERT(buf != NULL);
		cmd_i->ntc_errno = cmd_xcode(C2_NET_TEST_ENCODE,
					     cmd_i, buf, 0, NULL);
	}

	result = commands_network_operation(ctx, true);
	commands_disable_succesful(ctx);
	return result;
}

uint32_t c2_net_test_commands_wait(struct c2_net_test_cmd_ctx *ctx)
{
	int			i;
	uint32_t		result;
	struct c2_net_test_cmd *cmd_i;
	struct c2_net_buffer   *buf;

	C2_PRE(c2_net_test_commands_invariant(ctx));

	/* set c2_net_test_cmd.ntc_buf_status to C2_NET_TEST_CMD_NOT_RECEIVED */
	for (i = 0; i < ctx->ntcc_cmd_nr; ++i)
		if (!c2_net_test_command(ctx, i)->ntc_disabled)
			c2_net_test_command(ctx, i)->ntc_buf_status =
				C2_NET_TEST_CMD_NOT_RECEIVED;

	/* blocking network receive */
	result = commands_network_operation(ctx, false);

	/* decode all received buffers */
	for (i = 0; i < ctx->ntcc_cmd_nr; ++i) {
		cmd_i = c2_net_test_command(ctx, i);
		/* command is disabled of receiving failed */
		if (!cmd_success(cmd_i))
			continue;

		/* decode buffer */
		buf = c2_net_test_network_buf(&ctx->ntcc_net,
					      C2_NET_TEST_BUF_PING,
					      cmd_i->ntc_buf_index);
		C2_ASSERT(buf != NULL);
		cmd_i->ntc_errno = cmd_xcode(C2_NET_TEST_DECODE,
					     cmd_i, buf, 0, NULL);
	}

	commands_disable_succesful(ctx);
	return result;
}

void c2_net_test_commands_reset(struct c2_net_test_cmd_ctx *ctx)
{
	uint32_t i;

	C2_PRE(c2_net_test_commands_invariant(ctx));
	for (i = 0; i < ctx->ntcc_cmd_nr; ++i)
		C2_SET0(&ctx->ntcc_cmd[i]);
}

struct c2_net_test_cmd *
c2_net_test_command(struct c2_net_test_cmd_ctx *ctx, uint32_t index)
{
	C2_PRE(ctx != NULL);
	C2_PRE(index < ctx->ntcc_cmd_nr);

	return &ctx->ntcc_cmd[index];
}

bool c2_net_test_commands_invariant(struct c2_net_test_cmd_ctx *ctx)
{

	if (ctx == NULL)
		return false;
	if (ctx->ntcc_cmd_nr == 0)
		return false;
	if (ctx->ntcc_cmd_nr != ctx->ntcc_net.ntc_ep_nr)
		return false;
	if (ctx->ntcc_cmd_nr != ctx->ntcc_net.ntc_buf_ping_nr)
		return false;
	if (ctx->ntcc_net.ntc_buf_bulk_nr != 0)
		return false;
	return true;
}

/**
   @} end NetTestCommandsInternals
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
