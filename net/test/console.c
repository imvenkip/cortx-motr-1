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
#include "lib/errno.h"		/* ETIMEDOUT */

#include "net/test/console.h"


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
				       cfg->ntcc_data_servers :
				       cfg->ntcc_data_clients;
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
	struct c2_net_test_slist	    *nodes_data;
	bool				     role_client;
	c2_time_t			     deadline;
	size_t				     success_nr = 0;
	size_t				     rcvd_nr = 0;
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
	nodes_data   = role_client ? &cfg->ntcc_data_clients :
				     &cfg->ntcc_data_servers;
	rctx	     = role_client ? &ctx->ntcc_clients : &ctx->ntcc_servers;
	cmd_ctx	     = rctx->ntcrc_cmd;

	/** @todo clear recv queue */
	/* send all commands */
	for (i = 0; i < nodes->ntsl_nr; ++i) {
		if (cmd_type == C2_NET_TEST_CMD_INIT)
			cmd.ntc_init.ntci_tm_ep = nodes_data->ntsl_list[i];
		cmd.ntc_ep_index     = i;
		rctx->ntcrc_errno[i] = c2_net_test_commands_send(cmd_ctx, &cmd);
	}
	c2_net_test_commands_send_wait_all(cmd_ctx);

	/* receive answers */
	if (answer[cmd_type] == C2_NET_TEST_CMD_STATUS_DATA) {
		sd = rctx->ntcrc_sd;
		C2_SET0(sd);
		c2_net_test_stats_init(&sd->ntcsd_bandwidth_1s_send);
		c2_net_test_stats_init(&sd->ntcsd_bandwidth_1s_recv);
		c2_net_test_stats_init(&sd->ntcsd_rtt);
	}
	deadline = c2_time_add(c2_time_now(), cfg->ntcc_cmd_recv_timeout);
	while (!c2_time_after(c2_time_now(), deadline) &&
	       rcvd_nr < nodes->ntsl_nr) {
		rc = c2_net_test_commands_recv(cmd_ctx, &cmd, deadline);
		/* deadline reached */
		if (rc == -ETIMEDOUT)
			break;
		/** @todo possible spinlock if all recv fails instantly? */
		if (rc != 0)
			continue;
		rcvd_nr++;
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

	LOGD("console: rc = %d\n", rc);

	return success_nr;
}

/**
   @} end of NetTestConsoleInternals group
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
