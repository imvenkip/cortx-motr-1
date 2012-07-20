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

#include "net/test/client.h"

/**
   @defgroup NetTestClientInternals Test Client
   @ingroup NetTestInternals

   @todo split this file.

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

static void node_cb_impossible(struct c2_net_test_network_ctx *ctx,
			 const uint32_t buf_index,
			 enum c2_net_queue_type q,
			 const struct c2_net_buffer_event *ev)
{
	C2_IMPOSSIBLE("Impossible callback.");
}

static void ping_server_msg_recv(struct c2_net_test_network_ctx *ctx,
			     const uint32_t buf_index,
			     enum c2_net_queue_type q,
			     const struct c2_net_buffer_event *ev)
{
	/** @todo add buffer to C2_NET_QT_MSG_SEND queue */
	/** @todo update stats */
}

static void ping_server_msg_send(struct c2_net_test_network_ctx *ctx,
				 const uint32_t buf_index,
				 enum c2_net_queue_type q,
				 const struct c2_net_buffer_event *ev)
{
	/** @todo add buffer to C2_NET_QT_MSG_RECV queue */
	/** @todo update stats */
}

static struct c2_net_test_network_buffer_callbacks ping_server_buf_cb = {
	.ntnbc_cb = {
		[C2_NET_QT_MSG_RECV]		= ping_server_msg_recv,
		[C2_NET_QT_MSG_SEND]		= ping_server_msg_send,
		[C2_NET_QT_PASSIVE_BULK_RECV]	= node_cb_impossible,
		[C2_NET_QT_PASSIVE_BULK_SEND]	= node_cb_impossible,
		[C2_NET_QT_ACTIVE_BULK_RECV]	= node_cb_impossible,
		[C2_NET_QT_ACTIVE_BULK_SEND]	= node_cb_impossible,
	}
};

#if 0
static void ping_client_msg_recv(struct c2_net_test_network_ctx *ctx,
			     const uint32_t buf_index,
			     enum c2_net_queue_type q,
			     const struct c2_net_buffer_event *ev)
{
	/** @todo add buffer to msg recv queue */
	/** @todo update stats */
}

static void ping_client_msg_send(struct c2_net_test_network_ctx *ctx,
				 const uint32_t buf_index,
				 enum c2_net_queue_type q,
				 const struct c2_net_buffer_event *ev)
{
	/** @todo add buffer to msg send queue */
	/** @todo update stats */
}

static struct c2_net_test_network_buffer_callbacks ping_client_buf_cb = {
	.ntnbc_cb = {
		[C2_NET_QT_MSG_RECV]		= ping_client_msg_recv,
		[C2_NET_QT_MSG_SEND]		= ping_client_msg_send,
		[C2_NET_QT_PASSIVE_BULK_RECV]	= node_cb_impossible,
		[C2_NET_QT_PASSIVE_BULK_SEND]	= node_cb_impossible,
		[C2_NET_QT_ACTIVE_BULK_RECV]	= node_cb_impossible,
		[C2_NET_QT_ACTIVE_BULK_SEND]	= node_cb_impossible,
	}
};
#endif

static int ping_server_handler_init_fini(struct c2_net_test_node_ctx *ctx,
					 const struct c2_net_test_cmd *cmd,
					 bool init)
{
	struct c2_net_test_network_timeouts timeouts;
	int				    rc = 0;

	/** @todo add preconditions */
	if (!init)
		goto fini;

	rc = c2_net_test_network_ctx_init(&ctx->ntnc_net,
					  cmd->ntc_init.ntci_tm_ep,
					  &node_tm_cb,
					  &ping_server_buf_cb,
					  cmd->ntc_init.ntci_buf_nr,
					  cmd->ntc_init.ntci_msg_size,
					  0, 0,
					  cmd->ntc_init.ntci_ep.ntsl_nr,
					  &timeouts);

	goto exit;
	/** @todo add all buffers to recv queue */
fini:
	c2_net_test_network_ctx_fini(&ctx->ntnc_net);
exit:
	return rc;
}

static int ping_server_init(struct c2_net_test_node_ctx *ctx)
{
	return 0;
}

static int ping_server_fini(struct c2_net_test_node_ctx *ctx)
{
	/**
	 * @todo finalize network context if INIT command was received
	 * and succesfully handled.
	 */
	/*
	int rc = ping_server_handler_init_fini(ctx, NULL, true);
	*/
	return 0;
}

static int ping_server_step(struct c2_net_test_node_ctx *ctx)
{
	/** @todo nothing for now... */
	return 0;
}

static int ping_server_handler_init(struct c2_net_test_node_ctx *ctx,
				    const struct c2_net_test_cmd *cmd)
{
	return ping_server_handler_init_fini(ctx, cmd, false);
}

static int ping_server_handler_stop(struct c2_net_test_node_ctx *ctx,
				    const struct c2_net_test_cmd *cmd)
{
	/** @todo implement */
	ctx->ntnc_exit_flag = true;
	return 0;
}

static struct c2_net_test_service_cmd_handler ping_server_cmd_handler[] = {
	{
		.ntsch_type    = C2_NET_TEST_CMD_INIT,
		.ntsch_handler = ping_server_handler_init,
	},
	{
		.ntsch_type    = C2_NET_TEST_CMD_STOP,
		.ntsch_handler = ping_server_handler_stop,
	},
};

static struct c2_net_test_service_ops ping_server_ops = {
	.ntso_init	     = ping_server_init,
	.ntso_fini	     = ping_server_fini,
	.ntso_step	     = ping_server_step,
	.ntso_cmd_handler    = ping_server_cmd_handler,
	.ntso_cmd_handler_nr = ARRAY_SIZE(ping_server_cmd_handler),
};

static struct c2_net_test_service_ops
*get_service_ops(struct c2_net_test_cmd *cmd)
{
	C2_PRE(cmd->ntc_type == C2_NET_TEST_CMD_INIT);

	if (cmd->ntc_init.ntci_type == C2_NET_TEST_TYPE_PING) {
		if (cmd->ntc_init.ntci_role == C2_NET_TEST_ROLE_SERVER) {
			return &ping_server_ops;
		} else {
			return NULL;
		}
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
	int rc;

	C2_PRE(ctx != NULL);
	do {
		rc = node_cmd_get(&ctx->ntnc_cmd, cmd,
			c2_time_from_now(0, 20 * (C2_TIME_ONE_BILLION / 1000)));
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
	int				rc;

	C2_PRE(ctx != NULL);

	/* wait for INIT command */
	rc = node_cmd_wait(ctx, &cmd, C2_NET_TEST_CMD_INIT);
	if (ctx->ntnc_exit_flag)
		return c2_net_test_commands_received_free(&cmd);
	if ((ctx->ntnc_errno = rc) != 0)
		return;
	/* we have configuration; initialize test service */
	svc_ops = get_service_ops(&cmd);
	if (svc_ops == NULL)
		return c2_net_test_commands_received_free(&cmd);
	rc = c2_net_test_service_init(&svc, ctx, svc_ops);
	if (rc != 0)
		return c2_net_test_commands_received_free(&cmd);
	/* test service is initialized. start main loop */
	do {
		/* get command */
		rc = node_cmd_get(&ctx->ntnc_cmd, &cmd, c2_time_now());
		if (rc != 0 && rc != -ETIMEDOUT)
			break;
		if (rc == 0) {
			/* we have command. handle it */
			rc = c2_net_test_service_cmd_handle(&svc, &cmd);
			c2_net_test_commands_received_free(&cmd);
		} else {
			/* we haven't command. take a step. */
			c2_net_test_service_step(&svc);
		}
		svc_state = c2_net_test_service_state_get(&svc);
	} while (svc_state != C2_NET_TEST_SERVICE_FAILED &&
		 svc_state != C2_NET_TEST_SERVICE_FINISHED &&
		 !ctx->ntnc_exit_flag &&
		 rc == 0);

	ctx->ntnc_errno = rc;
	/* finalize test service */
	c2_net_test_service_fini(&svc);
	return;
}

static int node_init_fini(struct c2_net_test_node_ctx *ctx,
			  struct c2_net_test_node_cfg *cfg,
			  bool init)
{
	struct c2_net_test_slist ep_list;
	int			 rc = 0;

	C2_PRE(ctx != NULL);
	if (!init)
		goto fini;
	C2_PRE(cfg != NULL);

	rc = c2_net_test_slist_init(&ep_list, cfg->ntnc_addr_console, '`');
	if (rc != 0)
		goto failed;
	rc = c2_net_test_commands_init(&ctx->ntnc_cmd,
				       cfg->ntnc_addr,
				       cfg->ntnc_send_timeout,
				       NULL,	/** @todo fix */
				       &ep_list);
	c2_net_test_slist_fini(&ep_list);
	if (rc != 0)
		goto failed;

	rc = c2_semaphore_init(&ctx->ntnc_sem_cmd_sent, 0);
	if (rc != 0)
		goto fini_cmd;

fini:
//fini_sem_cmd_sent:
	c2_semaphore_fini(&ctx->ntnc_sem_cmd_sent);
fini_cmd:
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
	C2_ASSERT(rc == 0);
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
	c2_semaphore_up(&ctx->ntnc_sem_cmd_sent);
	rc = c2_thread_join(&ctx->ntnc_thread);
	/*
	 * In either case when rc != 0 there is an unmatched
	 * c2_net_test_node_start() and c2_net_test_node_stop()
	 * or deadlock. If rc is returned as result of this function,
	 * then c2_net_test_node_stop() leaves c2_net_test_node_ctx in
	 * inconsistent state (also possible resource leak).
	 */
	C2_ASSERT(rc == 0);
	c2_thread_fini(&ctx->ntnc_thread);
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

static int console_init_fini(struct c2_net_test_console_ctx *ctx,
			     struct c2_net_test_console_cfg *cfg,
			     bool init)
{
	return -ENOSYS;
}

int c2_net_test_console_init(struct c2_net_test_console_ctx *ctx,
			     struct c2_net_test_console_cfg *cfg)
{
	return console_init_fini(ctx, cfg, true);
}

void c2_net_test_console_fini(struct c2_net_test_console_ctx *ctx)
{
	int rc = console_init_fini(ctx, NULL, false);
	C2_ASSERT(rc == 0);
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

int c2_net_test_service_init(struct c2_net_test_service *svc,
			     struct c2_net_test_node_ctx *node_ctx,
			     struct c2_net_test_service_ops *ops)
{
	C2_PRE(svc != NULL);
	C2_PRE(node_ctx != NULL);
	C2_PRE(ops != NULL);

	svc->nts_node_ctx = node_ctx;
	svc->nts_ops	  = ops;

	svc->nts_errno = svc->nts_ops->ntso_init(svc->nts_node_ctx);
	if (svc->nts_errno == 0)
		c2_net_test_service_state_change(svc,
				C2_NET_TEST_SERVICE_READY);

	return svc->nts_errno;
}

void c2_net_test_service_fini(struct c2_net_test_service *svc)
{
	C2_PRE(c2_net_test_service_invariant(svc));

	svc->nts_errno = svc->nts_ops->ntso_fini(svc->nts_node_ctx);
	c2_net_test_service_state_change(svc,
			C2_NET_TEST_SERVICE_UNINITIALIZED);

	C2_POST(svc->nts_errno == 0);
}

bool c2_net_test_service_invariant(struct c2_net_test_service *svc)
{
	/** @todo improve invariant */
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
				   struct c2_net_test_cmd *cmd)
{
	struct c2_net_test_service_cmd_handler *handler;
	int					i;

	C2_PRE(c2_net_test_service_invariant(svc));
	C2_PRE(cmd != NULL);
	C2_PRE(svc->nts_state == C2_NET_TEST_SERVICE_READY);

	svc->nts_errno = -ENOENT;
	for (i = 0; i < svc->nts_ops->ntso_cmd_handler_nr; ++i) {
		handler = &svc->nts_ops->ntso_cmd_handler[i];
		if (handler->ntsch_type == cmd->ntc_type) {
			svc->nts_errno = handler->ntsch_handler(
					 svc->nts_node_ctx, cmd);
			break;
		}
	}

	C2_POST(c2_net_test_service_invariant(svc));
	return svc->nts_errno;
}

void c2_net_test_service_state_change(struct c2_net_test_service *svc,
				      enum c2_net_test_service_state state)
{
	/** @todo add state transition checks */
	svc->nts_state = state;
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
