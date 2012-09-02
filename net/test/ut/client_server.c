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
 * Original creation date: 05/19/2012
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

#include "net/test/node_config.h"	/* c2_net_test_type */
#include "net/test/client.h"		/* c2_net_test_node_cfg */

#include "lib/ut.h"			/* C2_UT_ASSERT */
#include "lib/memory.h"			/* c2_free */
#include "lib/thread.h"			/* C2_THREAD_INIT */
#include "lib/semaphore.h"		/* c2_semaphore_down */
#include "lib/misc.h"			/* C2_SET0 */
#include "net/lnet/lnet.h"		/* C2_NET_LNET_PID */

enum {
	NTCS_PID		  = C2_NET_LNET_PID,
	NTCS_PORTAL		  = 30,
	NTCS_NODES_MAX		  = 128,
	NTCS_NODE_ADDR_MAX	  = 0x100,
	NTCS_TIMEOUT_SEND_MS	  = 1000,
	NTCS_TIMEOUT_RECV_MS	  = 1000,
	NTCS_TMID_CONSOLE4CLIENTS = 2998,
	NTCS_TMID_CONSOLE4SERVERS = 2999,
	NTCS_TMID_NODES		  = 3000,
	NTCS_TMID_CMD_CLIENTS	  = NTCS_TMID_NODES,
	NTCS_TMID_DATA_CLIENTS    = NTCS_TMID_NODES + NTCS_NODES_MAX * 1,
	NTCS_TMID_CMD_SERVERS	  = NTCS_TMID_NODES + NTCS_NODES_MAX * 2,
	NTCS_TMID_DATA_SERVERS    = NTCS_TMID_NODES + NTCS_NODES_MAX * 3,
};

static struct c2_net_test_node_cfg node_cfg[NTCS_NODES_MAX * 2];
static struct c2_thread		   node_thread[NTCS_NODES_MAX * 2];

static char *addr_console4clients;
static char *addr_console4servers;
static char  clients[(NTCS_NODES_MAX + 1) * NTCS_NODE_ADDR_MAX];
static char  servers[(NTCS_NODES_MAX + 1) * NTCS_NODE_ADDR_MAX];

static char *addr_get(const char *nid, int tmid)
{
	char  addr[NTCS_NODE_ADDR_MAX];
	char *result;
	int   rc;

	rc = snprintf(addr, NTCS_NODE_ADDR_MAX,
		     "%s:%d:%d:%d", nid, NTCS_PID, NTCS_PORTAL, tmid);
	C2_UT_ASSERT(rc < NTCS_NODE_ADDR_MAX);

	result = c2_alloc(rc + 1);
	C2_UT_ASSERT(result != NULL);
	return strncpy(result, addr, rc + 1);
}

static void addr_free(char *addr)
{
	c2_free(addr);
}

static void net_test_node(struct c2_net_test_node_cfg *node_cfg)
{
	struct c2_net_test_node_ctx *ctx;
	int			     rc;

	C2_PRE(node_cfg != NULL);

	C2_ALLOC_PTR(ctx);
	C2_ASSERT(ctx != NULL);
	rc = c2_net_test_node_init(ctx, node_cfg);
	C2_UT_ASSERT(rc == 0);
	rc = c2_net_test_node_start(ctx);
	C2_UT_ASSERT(rc == 0);
	/* wait for the test node thread */
	c2_semaphore_down(&ctx->ntnc_thread_finished_sem);
	c2_net_test_node_stop(ctx);
	c2_net_test_node_fini(ctx);
	c2_free(ctx);
}

static c2_time_t ms2time(int ms)
{
	c2_time_t time;

	return c2_time_set(&time,NTCS_TIMEOUT_SEND_MS / 1000,
			   (NTCS_TIMEOUT_SEND_MS % 1000) * 1000000);
}

/*
 * Real situation - no explicit synchronization
 * between test console and test nodes.
 */
static void net_test_client_server(const char *nid,
				   enum c2_net_test_type type,
				   size_t clients_nr,
				   size_t servers_nr,
				   size_t concurrency_client,
				   size_t concurrency_server,
				   size_t msg_nr,
				   c2_bcount_t msg_size)
{
	struct c2_net_test_console_cfg console_cfg;
	struct c2_net_test_console_ctx console;
	int			       rc;
	int			       i;
	c2_time_t		       timeout_send;
	c2_time_t		       timeout_recv;
	struct c2_net_test_node_cfg   *ncfg;

	C2_PRE(clients_nr <= NTCS_NODES_MAX);
	C2_PRE(servers_nr <= NTCS_NODES_MAX);
	/* prepare config for test clients and test servers */
	timeout_send = ms2time(NTCS_TIMEOUT_SEND_MS);
	timeout_recv = ms2time(NTCS_TIMEOUT_RECV_MS);
	addr_console4clients = addr_get(nid, NTCS_TMID_CONSOLE4CLIENTS);
	addr_console4servers = addr_get(nid, NTCS_TMID_CONSOLE4SERVERS);
	clients[0] = '\0';
	for (i = 0; i < clients_nr; ++i) {
		ncfg = &node_cfg[i];
		ncfg->ntnc_addr = addr_get(nid, NTCS_TMID_CMD_CLIENTS + i);
		ncfg->ntnc_addr_console = addr_console4clients;
		ncfg->ntnc_send_timeout = timeout_send;
		strncat(clients, ncfg->ntnc_addr, NTCS_NODE_ADDR_MAX);
		strncat(clients, i == clients_nr - 1 ? "" : ",", 2);
	}
	servers[0] = '\0';
	for (i = 0; i < servers_nr; ++i) {
		ncfg = &node_cfg[clients_nr + i];
		ncfg->ntnc_addr = addr_get(nid, NTCS_TMID_CMD_SERVERS + i);
		ncfg->ntnc_addr_console = addr_console4servers;
		ncfg->ntnc_send_timeout = timeout_send;
		strncat(servers, ncfg->ntnc_addr, NTCS_NODE_ADDR_MAX);
		strncat(servers, i == servers_nr - 1 ? "" : ",", 2);
	}
	/* spawn test clients and test servers */
	for (i = 0; i < clients_nr + servers_nr; ++i) {
		rc = C2_THREAD_INIT(&node_thread[i],
				    struct c2_net_test_node_cfg *,
				    NULL, &net_test_node, &node_cfg[i],
				    "node_thread#%d", i);
		C2_UT_ASSERT(rc == 0);
	}
	/* prepare console config */
	console_cfg.ntcc_addr_console4servers = addr_console4servers;
	console_cfg.ntcc_addr_console4clients = addr_console4clients;
	rc = c2_net_test_slist_init(&console_cfg.ntcc_clients, clients, ',');
	C2_UT_ASSERT(rc == 0);
	rc = c2_net_test_slist_init(&console_cfg.ntcc_servers, servers, ',');
	C2_UT_ASSERT(rc == 0);
	console_cfg.ntcc_cmd_send_timeout   = timeout_send;
	console_cfg.ntcc_cmd_recv_timeout   = timeout_recv;
	console_cfg.ntcc_buf_send_timeout   = timeout_send;
	console_cfg.ntcc_buf_recv_timeout   = timeout_recv;
	console_cfg.ntcc_test_type	    = type;
	console_cfg.ntcc_msg_nr		    = msg_nr;
	console_cfg.ntcc_msg_size	    = msg_size;
	console_cfg.ntcc_concurrency_server = concurrency_server;
	console_cfg.ntcc_concurrency_client = concurrency_client;
	/* initialize console */
	rc = c2_net_test_console_init(&console, &console_cfg);
	C2_UT_ASSERT(rc == 0);
	/* send INIT to the test servers */
	rc = c2_net_test_console_cmd(&console, C2_NET_TEST_ROLE_SERVER,
				     C2_NET_TEST_CMD_INIT);
	LOGD("%d\n", rc);
	C2_UT_ASSERT(rc == servers_nr);
	/* send INIT to the test clients */
	/* finalize console */
	c2_net_test_slist_fini(&console_cfg.ntcc_servers);
	c2_net_test_slist_fini(&console_cfg.ntcc_clients);
	c2_net_test_console_fini(&console);
	/* finalize test clients and test servers */
	for (i = 0; i < clients_nr + servers_nr; ++i) {
		rc = c2_thread_join(&node_thread[i]);
		C2_UT_ASSERT(rc == 0);
		c2_thread_fini(&node_thread[i]);
		addr_free(node_cfg[i].ntnc_addr);
	}
	addr_free(addr_console4servers);
	addr_free(addr_console4clients);
}

void c2_net_test_client_server_ping_ut(void)
{
	net_test_client_server("0@lo", C2_NET_TEST_TYPE_PING,
			       1, 1, 1, 1, 1, 0x100);
	//net_test_client_server("0@lo", C2_NET_TEST_TYPE_PING,
	//		       8, 8, 4, 16, 0x100, 0x100);
}

void c2_net_test_client_server_bulk_ut(void)
{
	net_test_client_server("0@lo", C2_NET_TEST_TYPE_BULK,
			       8, 8, 4, 16, 0x100, 0x10000);
}

/**
 ***********************************************************************
 ***********************************************************************
 ***********************************************************************
 ***********************************************************************
 ***********************************************************************
 ***********************************************************************
 ***********************************************************************
 */

enum {
	SERVICE_ITERATIONS_NR	= 0x1000,
};

static struct c2_net_test_node_ctx service_ut_node_ctx;
static struct c2_net_test_cmd	   service_cmd;
static struct c2_net_test_cmd	   service_reply;
static bool			   service_init_called;
static bool			   service_fini_called;
static bool			   service_step_called;
static bool			   service_cmd_called[C2_NET_TEST_CMD_NR];
static int			   service_cmd_errno;

static bool *service_func_called[] = {
	&service_init_called,
	&service_fini_called,
	&service_step_called
};

static int service_ut_cmd(struct c2_net_test_node_ctx *node_ctx,
			  const struct c2_net_test_cmd *cmd,
			  struct c2_net_test_cmd *reply,
			  enum c2_net_test_cmd_type cmd_type)
{
	service_cmd_called[cmd_type] = true;
	C2_UT_ASSERT(node_ctx == &service_ut_node_ctx);
	C2_UT_ASSERT(cmd == &service_cmd);
	C2_UT_ASSERT(reply == &service_reply);
	return service_cmd_errno;
}

static int service_ut_cmd_init(struct c2_net_test_node_ctx *node_ctx,
			       const struct c2_net_test_cmd *cmd,
			       struct c2_net_test_cmd *reply)
{
	return service_ut_cmd(node_ctx, cmd, reply, C2_NET_TEST_CMD_INIT);
}

static int service_ut_cmd_start(struct c2_net_test_node_ctx *node_ctx,
				const struct c2_net_test_cmd *cmd,
				struct c2_net_test_cmd *reply)
{
	return service_ut_cmd(node_ctx, cmd, reply, C2_NET_TEST_CMD_START);
}

static int service_ut_cmd_stop(struct c2_net_test_node_ctx *node_ctx,
			       const struct c2_net_test_cmd *cmd,
			       struct c2_net_test_cmd *reply)
{
	return service_ut_cmd(node_ctx, cmd, reply, C2_NET_TEST_CMD_STOP);
}

static int service_ut_cmd_status(struct c2_net_test_node_ctx *node_ctx,
				 const struct c2_net_test_cmd *cmd,
				 struct c2_net_test_cmd *reply)
{
	return service_ut_cmd(node_ctx, cmd, reply, C2_NET_TEST_CMD_STATUS);
}

static int service_ut_init(struct c2_net_test_node_ctx *node_ctx)
{
	service_init_called = true;
	return 0;
}

static void service_ut_fini(struct c2_net_test_node_ctx *node_ctx)
{
	service_fini_called = true;
}

static int service_ut_step(struct c2_net_test_node_ctx *node_ctx)
{
	service_step_called = true;
	return 0;
}

static struct c2_net_test_service_cmd_handler service_ut_cmd_handler[] = {
	{
		.ntsch_type    = C2_NET_TEST_CMD_INIT,
		.ntsch_handler = service_ut_cmd_init,
	},
	{
		.ntsch_type    = C2_NET_TEST_CMD_START,
		.ntsch_handler = service_ut_cmd_start,
	},
	{
		.ntsch_type    = C2_NET_TEST_CMD_STOP,
		.ntsch_handler = service_ut_cmd_stop,
	},
	{
		.ntsch_type    = C2_NET_TEST_CMD_STATUS,
		.ntsch_handler = service_ut_cmd_status,
	},
};

static struct c2_net_test_service_ops service_ut_ops = {
	.ntso_init	     = service_ut_init,
	.ntso_fini	     = service_ut_fini,
	.ntso_step	     = service_ut_step,
	.ntso_cmd_handler    = service_ut_cmd_handler,
	.ntso_cmd_handler_nr = ARRAY_SIZE(service_ut_cmd_handler),
};

static void service_ut_checks(struct c2_net_test_service *svc,
			      enum c2_net_test_service_state state)
{
	enum c2_net_test_service_state svc_state;
	bool			       rc_bool;

	rc_bool = c2_net_test_service_invariant(svc);
	C2_UT_ASSERT(rc_bool);
	svc_state = c2_net_test_service_state_get(svc);
	C2_UT_ASSERT(svc_state == state);
}

static void service_ut_check_reset(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(service_func_called); ++i)
		*service_func_called[i] = false;
	C2_SET_ARR0(service_cmd_called);
}

static void service_ut_check_called(bool *func_bool)
{
	size_t func_nr = ARRAY_SIZE(service_func_called);
	size_t cmd_nr  = ARRAY_SIZE(service_cmd_called);
	bool   called;
	bool  *called_i;
	int    called_nr = 0;
	int    i;

	C2_PRE(func_bool != NULL);

	for (i = 0; i < func_nr + cmd_nr; ++i) {
		called_i = i < func_nr ? service_func_called[i] :
					 &service_cmd_called[i - func_nr];
		called = func_bool == called_i;
		C2_UT_ASSERT(equi(called, *called_i));
		called_nr += *called_i;
	}
	C2_UT_ASSERT(called_nr == 1);
}

bool service_can_handle(enum c2_net_test_cmd_type cmd_type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(service_ut_cmd_handler); ++i) {
		if (service_ut_cmd_handler[i].ntsch_type == cmd_type)
			return true;
	}
	return false;
}

void c2_net_test_service_ut(void)
{
	struct c2_net_test_service svc;
	enum c2_net_test_cmd_type  cmd_type;
	int			   rc;
	uint64_t		   seed = 42;
	int			   i;
	int			   cmd_max;
	int			   cmd_index;

	C2_SET0(&service_ut_node_ctx);

	/* test c2_net_test_service_init() */
	service_ut_check_reset();
	rc = c2_net_test_service_init(&svc, &service_ut_node_ctx,
				      &service_ut_ops);
	C2_UT_ASSERT(rc == 0);
	service_ut_checks(&svc, C2_NET_TEST_SERVICE_READY);
	service_ut_check_called(&service_init_called);

	/* test c2_net_test_service_step()/c2_net_test_service_cmd_handle() */
	cmd_max = ARRAY_SIZE(service_cmd_called) + 1;
	for (i = 0; i < SERVICE_ITERATIONS_NR; ++i)
	{
		cmd_index = c2_rnd(cmd_max, &seed);
		if (cmd_index == cmd_max - 1) {
			/* step */
			service_ut_check_reset();
			rc = c2_net_test_service_step(&svc);
			C2_UT_ASSERT(rc == 0);
			service_ut_check_called(&service_step_called);
			service_ut_checks(&svc, C2_NET_TEST_SERVICE_READY);
		} else {
			/* command */
			cmd_type = cmd_index;
			service_cmd_errno = service_can_handle(cmd_type) ?
					    -c2_rnd(64, &seed) : -ENOENT;
			service_ut_check_reset();
			service_cmd.ntc_type = cmd_type;
			rc = c2_net_test_service_cmd_handle(&svc, &service_cmd,
							    &service_reply);
			C2_UT_ASSERT(rc == service_cmd_errno);
			service_ut_checks(&svc, C2_NET_TEST_SERVICE_READY);
		}
	}

	/* test C2_NET_TEST_SERVICE_FAILED state */
	c2_net_test_service_state_change(&svc, C2_NET_TEST_SERVICE_FAILED);
	service_ut_checks(&svc, C2_NET_TEST_SERVICE_FAILED);

	/* test c2_net_test_service_fini() */
	service_ut_check_reset();
	c2_net_test_service_fini(&svc);
	service_ut_check_called(&service_fini_called);
}


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
