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

#include "lib/memory.h"		/* M0_ALLOC_PTR */
#include "lib/misc.h"		/* M0_SET0 */
#include "lib/errno.h"		/* ETIMEDOUT */

#include "net/test/console.h"


/**
   @defgroup NetTestConsoleInternals Test Console
   @ingroup NetTestInternals

   @see
   @ref net-test

   @{
 */

static int console_role_init_fini(struct m0_net_test_console_role_ctx *ctx,
				 struct m0_net_test_console_cfg *cfg,
				 enum m0_net_test_role role,
				 bool init)
{
	struct m0_net_test_slist *nodes;
	char			 *addr_console;
	int			  rc = -ENOMEM;

	if (!init)
		goto fini;

	addr_console = role == M0_NET_TEST_ROLE_CLIENT ?
		cfg->ntcc_addr_console4clients : cfg->ntcc_addr_console4servers;
	nodes = role == M0_NET_TEST_ROLE_CLIENT ?
		&cfg->ntcc_clients : &cfg->ntcc_servers;

	M0_ALLOC_PTR(ctx->ntcrc_cmd);
	if (ctx->ntcrc_cmd == NULL)
		goto fail;
	M0_ALLOC_PTR(ctx->ntcrc_sd);
	if (ctx->ntcrc_sd == NULL)
		goto fini_cmd;
	ctx->ntcrc_nr = nodes->ntsl_nr;
	M0_ALLOC_ARR(ctx->ntcrc_errno, ctx->ntcrc_nr);
	if (ctx->ntcrc_errno == NULL)
		goto fini_sd;
	M0_ALLOC_ARR(ctx->ntcrc_status, ctx->ntcrc_nr);
	if (ctx->ntcrc_status == NULL)
		goto fini_errno;

	rc = m0_net_test_commands_init(ctx->ntcrc_cmd, addr_console,
				       cfg->ntcc_cmd_send_timeout, NULL, nodes);
	if (rc != 0)
		goto fini_status;

	rc = 0;
	goto success;

fini:
	rc = 0;
	m0_net_test_commands_fini(ctx->ntcrc_cmd);
fini_status:
	m0_free(ctx->ntcrc_status);
fini_errno:
	m0_free(ctx->ntcrc_errno);
fini_sd:
	m0_free(ctx->ntcrc_sd);
fini_cmd:
	m0_free(ctx->ntcrc_cmd);
fail:
success:
	return rc;
}

static int console_init_fini(struct m0_net_test_console_ctx *ctx,
			     struct m0_net_test_console_cfg *cfg,
			     bool init)
{
	int rc;

	M0_PRE(ctx != NULL);
	M0_PRE(ergo(init, cfg != NULL));

	ctx->ntcc_cfg = cfg;
	rc = console_role_init_fini(&ctx->ntcc_clients, cfg,
				    M0_NET_TEST_ROLE_CLIENT, init);
	if (rc == 0)
		rc = console_role_init_fini(&ctx->ntcc_servers, cfg,
					    M0_NET_TEST_ROLE_SERVER, init);
	return rc;
}

int m0_net_test_console_init(struct m0_net_test_console_ctx *ctx,
			     struct m0_net_test_console_cfg *cfg)
{
	return console_init_fini(ctx, cfg, true);
}

void m0_net_test_console_fini(struct m0_net_test_console_ctx *ctx)
{
	int rc = console_init_fini(ctx, NULL, false);
	M0_POST(rc == 0);
}

static void console_cmd_init_fill(struct m0_net_test_console_cfg *cfg,
				  enum m0_net_test_role role,
				  struct m0_net_test_cmd_init *cinit)
{
	cinit->ntci_role	     = role;
	cinit->ntci_type	     = cfg->ntcc_test_type;
	cinit->ntci_msg_nr	     = cfg->ntcc_msg_nr;
	cinit->ntci_msg_size	     = cfg->ntcc_msg_size;
	cinit->ntci_concurrency      = role == M0_NET_TEST_ROLE_CLIENT ?
				       cfg->ntcc_concurrency_client :
				       cfg->ntcc_concurrency_server;
	cinit->ntci_buf_send_timeout = cfg->ntcc_buf_send_timeout;
	cinit->ntci_ep		     = role == M0_NET_TEST_ROLE_CLIENT ?
				       cfg->ntcc_data_servers :
				       cfg->ntcc_data_clients;
}

static void status_data_reset(struct m0_net_test_cmd_status_data *sd)
{
	M0_SET0(sd);
	m0_net_test_msg_nr_reset(&sd->ntcsd_msg_nr_send);
	m0_net_test_msg_nr_reset(&sd->ntcsd_msg_nr_recv);
	m0_net_test_stats_reset(&sd->ntcsd_mps_send.ntmps_stats);
	m0_net_test_stats_reset(&sd->ntcsd_mps_recv.ntmps_stats);
	m0_net_test_stats_reset(&sd->ntcsd_rtt);
	sd->ntcsd_finished    = true;
	sd->ntcsd_time_start  = M0_TIME_NEVER;
	sd->ntcsd_time_finish = 0;
}

static m0_time_t time_min(m0_time_t t1, m0_time_t t2)
{
	return t1 < t2 ? t1 : t2;
}

static m0_time_t time_max(m0_time_t t1, m0_time_t t2)
{
	return t1 > t2 ? t1 : t2;
}

static void status_data_add(struct m0_net_test_cmd_status_data *sd,
			    const struct m0_net_test_cmd_status_data *cmd_sd)
{
	LOGD("new STATUS_DATA:\n");
	LOGD("send total = %lu\n", cmd_sd->ntcsd_msg_nr_send.ntmn_total);
	LOGD("recv total = %lu\n", cmd_sd->ntcsd_msg_nr_recv.ntmn_total);
	LOGD("finished = %d\n", cmd_sd->ntcsd_finished);
	LOGD("end of STATUS_DATA\n");
	m0_net_test_msg_nr_add(&sd->ntcsd_msg_nr_send,
			       &cmd_sd->ntcsd_msg_nr_send);
	m0_net_test_msg_nr_add(&sd->ntcsd_msg_nr_recv,
			       &cmd_sd->ntcsd_msg_nr_recv);
	m0_net_test_stats_add_stats(&sd->ntcsd_mps_send.ntmps_stats,
				    &cmd_sd->ntcsd_mps_send.ntmps_stats);
	m0_net_test_stats_add_stats(&sd->ntcsd_mps_recv.ntmps_stats,
				    &cmd_sd->ntcsd_mps_recv.ntmps_stats);
	m0_net_test_stats_add_stats(&sd->ntcsd_rtt, &cmd_sd->ntcsd_rtt);
	sd->ntcsd_finished &= cmd_sd->ntcsd_finished;
	if (cmd_sd->ntcsd_finished) {
		sd->ntcsd_time_start = time_min(sd->ntcsd_time_start,
						cmd_sd->ntcsd_time_start);
		sd->ntcsd_time_finish = time_max(sd->ntcsd_time_finish,
						 cmd_sd->ntcsd_time_finish);
	}
}

size_t m0_net_test_console_cmd(struct m0_net_test_console_ctx *ctx,
			       enum m0_net_test_role role,
			       enum m0_net_test_cmd_type cmd_type)
{
	struct m0_net_test_console_role_ctx *rctx;
	struct m0_net_test_console_cfg	    *cfg;
	struct m0_net_test_cmd_ctx	    *cmd_ctx;
	struct m0_net_test_cmd		     cmd;
	struct m0_net_test_cmd_status_data  *sd = NULL;
	int				     i;
	int				     j;
	int				     rc;
	struct m0_net_test_slist	    *nodes;
	struct m0_net_test_slist	    *nodes_data;
	bool				     role_client;
	m0_time_t			     deadline;
	size_t				     success_nr = 0;
	size_t				     failures_nr = 0;
	size_t				     rcvd_nr = 0;
	enum m0_net_test_cmd_type	     answer[] = {
		[M0_NET_TEST_CMD_INIT]	 = M0_NET_TEST_CMD_INIT_DONE,
		[M0_NET_TEST_CMD_START]	 = M0_NET_TEST_CMD_START_DONE,
		[M0_NET_TEST_CMD_STOP]	 = M0_NET_TEST_CMD_STOP_DONE,
		[M0_NET_TEST_CMD_STATUS] = M0_NET_TEST_CMD_STATUS_DATA,
	};

	M0_PRE(ctx != NULL);
	M0_PRE(role == M0_NET_TEST_ROLE_SERVER ||
	       role == M0_NET_TEST_ROLE_CLIENT);
	M0_PRE(cmd_type == M0_NET_TEST_CMD_INIT ||
	       cmd_type == M0_NET_TEST_CMD_START ||
	       cmd_type == M0_NET_TEST_CMD_STOP ||
	       cmd_type == M0_NET_TEST_CMD_STATUS);

	M0_SET0(&cmd);
	cfg = ctx->ntcc_cfg;

	if (cmd_type == M0_NET_TEST_CMD_INIT)
		console_cmd_init_fill(cfg, role, &cmd.ntc_init);

	role_client  = role == M0_NET_TEST_ROLE_CLIENT;
	cmd.ntc_type = cmd_type;
	nodes	     = role_client ? &cfg->ntcc_clients : &cfg->ntcc_servers;
	nodes_data   = role_client ? &cfg->ntcc_data_clients :
				     &cfg->ntcc_data_servers;
	rctx	     = role_client ? &ctx->ntcc_clients : &ctx->ntcc_servers;
	cmd_ctx	     = rctx->ntcrc_cmd;

	/* clear commands receive queue */
	while ((rc = m0_net_test_commands_recv(cmd_ctx, &cmd, m0_time_now())) !=
	       -ETIMEDOUT) {
		/*
		 * Exit from this loop after nodes->ntsl_nr failures.
		 * It will prevent from infinite loop if after every
		 * m0_net_test_commands_recv_enqueue() will be
		 * unsuccessful m0_net_test_commands_recv().
		 */
		failures_nr += rc != 0;
		if (failures_nr > nodes->ntsl_nr)
			break;
		rc = m0_net_test_commands_recv_enqueue(cmd_ctx,
						       cmd.ntc_buf_index);
		/** @todo rc != 0 is lost here */
		m0_net_test_commands_received_free(&cmd);
	}
	/* send all commands */
	for (i = 0; i < nodes->ntsl_nr; ++i) {
		if (cmd_type == M0_NET_TEST_CMD_INIT)
			cmd.ntc_init.ntci_tm_ep = nodes_data->ntsl_list[i];
		cmd.ntc_ep_index     = i;
		rctx->ntcrc_errno[i] = m0_net_test_commands_send(cmd_ctx, &cmd);
	}
	m0_net_test_commands_send_wait_all(cmd_ctx);

	/* receive answers */
	if (answer[cmd_type] == M0_NET_TEST_CMD_STATUS_DATA) {
		sd = rctx->ntcrc_sd;
		status_data_reset(sd);
	}
	deadline = m0_time_add(m0_time_now(), cfg->ntcc_cmd_recv_timeout);
	while (m0_time_now() <= deadline && rcvd_nr < nodes->ntsl_nr) {
		rc = m0_net_test_commands_recv(cmd_ctx, &cmd, deadline);
		/* deadline reached */
		if (rc == -ETIMEDOUT)
			break;
		/** @todo possible spinlock if all recv fails instantly? */
		if (rc != 0)
			continue;
		rcvd_nr++;
		/* reject unknown sender */
		j = cmd.ntc_ep_index;
		if (j < 0)
			goto reuse_cmd;
		/* reject unexpected command type */
		if (cmd.ntc_type != answer[cmd_type])
			goto reuse_cmd;
		/*
		 * reject command from node, which can't have outgoing cmd
		 * because m0_net_test_commands_send() to this node failed.
		 */
		M0_ASSERT(j >= 0 && j < nodes->ntsl_nr);
		if (rctx->ntcrc_errno[j] != 0)
			goto reuse_cmd;
		/* handle incoming command */
		if (answer[cmd_type] == M0_NET_TEST_CMD_STATUS_DATA) {
			status_data_add(sd, &cmd.ntc_status_data);
			success_nr++;
		} else {
			rctx->ntcrc_status[j] = cmd.ntc_done.ntcd_errno;
			if (rctx->ntcrc_status[j] == 0)
				success_nr++;
		}
		/*
		 * @todo console user can't recover from this error -
		 * cmd.ntc_buf_index is lost. use ringbuf to save?
		 */
reuse_cmd:
		rc = m0_net_test_commands_recv_enqueue(cmd_ctx,
						       cmd.ntc_buf_index);
		if (j != -1) {
			M0_ASSERT(j >= 0 && j < nodes->ntsl_nr);
			rctx->ntcrc_errno[j] = rc;
		}
		m0_net_test_commands_received_free(&cmd);
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
