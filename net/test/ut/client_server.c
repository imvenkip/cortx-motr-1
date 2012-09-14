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

#include "net/test/node.h"		/* c2_net_test_node_ctx */
#include "net/test/console.h"		/* c2_net_test_console_ctx */

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
	NTCS_TIMEOUT_SEND_MS	  = 10000,
	NTCS_TIMEOUT_RECV_MS	  = 10000,
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
static struct c2_semaphore	   node_init_sem;

static char *addr_console4clients;
static char *addr_console4servers;
static char  clients[(NTCS_NODES_MAX + 1) * NTCS_NODE_ADDR_MAX];
static char  servers[(NTCS_NODES_MAX + 1) * NTCS_NODE_ADDR_MAX];
static char  clients_data[(NTCS_NODES_MAX + 1) * NTCS_NODE_ADDR_MAX];
static char  servers_data[(NTCS_NODES_MAX + 1) * NTCS_NODE_ADDR_MAX];
c2_time_t    timeout_send;
c2_time_t    timeout_recv;

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
	c2_semaphore_up(&node_init_sem);
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

static void node_cfg_fill(struct c2_net_test_node_cfg *ncfg,
			  char *addr_cmd,
			  char *addr_cmd_list,
			  char *addr_data,
			  char *addr_data_list,
			  char *addr_console,
			  bool last_node)
{
	ncfg->ntnc_addr		= addr_cmd;
	ncfg->ntnc_addr_console = addr_console;
	ncfg->ntnc_send_timeout = timeout_send;

	strncat(addr_cmd_list, ncfg->ntnc_addr, NTCS_NODE_ADDR_MAX);
	strncat(addr_cmd_list, last_node ? "" : ",", 2);
	strncat(addr_data_list, addr_data, NTCS_NODE_ADDR_MAX);
	strncat(addr_data_list, last_node ? "" : ",", 2);

	addr_free(addr_data);
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
	c2_time_t		       _1s;

	C2_PRE(clients_nr <= NTCS_NODES_MAX);
	C2_PRE(servers_nr <= NTCS_NODES_MAX);
	/* prepare config for test clients and test servers */
	timeout_send = ms2time(NTCS_TIMEOUT_SEND_MS);
	timeout_recv = ms2time(NTCS_TIMEOUT_RECV_MS);
	c2_time_set(&_1s, 1, 0);
	addr_console4clients = addr_get(nid, NTCS_TMID_CONSOLE4CLIENTS);
	addr_console4servers = addr_get(nid, NTCS_TMID_CONSOLE4SERVERS);
	clients[0] = '\0';
	for (i = 0; i < clients_nr; ++i) {
		node_cfg_fill(&node_cfg[i],
			      addr_get(nid, NTCS_TMID_CMD_CLIENTS + i), clients,
			      addr_get(nid, NTCS_TMID_DATA_CLIENTS + i),
			      clients_data, addr_console4clients,
			      i == clients_nr - 1);
	}
	servers[0] = '\0';
	for (i = 0; i < servers_nr; ++i) {
		node_cfg_fill(&node_cfg[clients_nr + i],
			      addr_get(nid, NTCS_TMID_CMD_SERVERS + i), servers,
			      addr_get(nid, NTCS_TMID_DATA_SERVERS + i),
			      servers_data, addr_console4servers,
			      i == servers_nr - 1);
	}
	/* spawn test clients and test servers */
	c2_semaphore_init(&node_init_sem, 0);
	for (i = 0; i < clients_nr + servers_nr; ++i) {
		rc = C2_THREAD_INIT(&node_thread[i],
				    struct c2_net_test_node_cfg *,
				    NULL, &net_test_node, &node_cfg[i],
				    "node_thread#%d", i);
		C2_UT_ASSERT(rc == 0);
	}
	/* wait until test node started */
	for (i = 0; i < clients_nr + servers_nr; ++i)
		c2_semaphore_down(&node_init_sem);
	c2_semaphore_fini(&node_init_sem);
	/* prepare console config */
	console_cfg.ntcc_addr_console4servers = addr_console4servers;
	console_cfg.ntcc_addr_console4clients = addr_console4clients;
	LOGD("addr_console4servers = %s\n", addr_console4servers);
	LOGD("addr_console4clients = %s\n", addr_console4clients);
	LOGD("clients		   = %s\n", clients);
	LOGD("servers		   = %s\n", servers);
	LOGD("clients_data	   = %s\n", clients_data);
	LOGD("servers_data	   = %s\n", servers_data);
	rc = c2_net_test_slist_init(&console_cfg.ntcc_clients, clients, ',');
	C2_UT_ASSERT(rc == 0);
	rc = c2_net_test_slist_init(&console_cfg.ntcc_servers, servers, ',');
	C2_UT_ASSERT(rc == 0);
	rc = c2_net_test_slist_init(&console_cfg.ntcc_data_clients,
				    clients_data, ',');
	C2_UT_ASSERT(rc == 0);
	rc = c2_net_test_slist_init(&console_cfg.ntcc_data_servers,
				    servers_data, ',');
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
	C2_UT_ASSERT(rc == servers_nr);
	/* send INIT to the test clients */
	rc = c2_net_test_console_cmd(&console, C2_NET_TEST_ROLE_CLIENT,
				     C2_NET_TEST_CMD_INIT);
	C2_UT_ASSERT(rc == clients_nr);
	/* send START command to the test servers */
	rc = c2_net_test_console_cmd(&console, C2_NET_TEST_ROLE_SERVER,
				     C2_NET_TEST_CMD_START);
	C2_UT_ASSERT(rc == servers_nr);
	/* send START command to the test clients */
	rc = c2_net_test_console_cmd(&console, C2_NET_TEST_ROLE_CLIENT,
				     C2_NET_TEST_CMD_START);
	C2_UT_ASSERT(rc == clients_nr);
	/* send STATUS command to the test clients until it finishes. */
	do {
		c2_nanosleep(_1s, NULL);
		rc = c2_net_test_console_cmd(&console, C2_NET_TEST_ROLE_CLIENT,
					     C2_NET_TEST_CMD_STATUS);
		C2_UT_ASSERT(rc == clients_nr);
	} while (!console.ntcc_clients.ntcrc_sd->ntcsd_finished);
	/* send STATUS command to the test clients */
	rc = c2_net_test_console_cmd(&console, C2_NET_TEST_ROLE_CLIENT,
				     C2_NET_TEST_CMD_STATUS);
	C2_UT_ASSERT(rc == clients_nr);
	C2_UT_ASSERT(console.ntcc_clients.ntcrc_sd->ntcsd_finished == true);
	LOGD("\nrecv total/failed/bad = %lu/%lu/%lu\n",
	     console.ntcc_clients.ntcrc_sd->ntcsd_msg_nr_recv.ntmn_total,
	     console.ntcc_clients.ntcrc_sd->ntcsd_msg_nr_recv.ntmn_failed,
	     console.ntcc_clients.ntcrc_sd->ntcsd_msg_nr_recv.ntmn_bad);
	LOGD("send total/failed/bad = %lu/%lu/%lu\n",
	     console.ntcc_clients.ntcrc_sd->ntcsd_msg_nr_send.ntmn_total,
	     console.ntcc_clients.ntcrc_sd->ntcsd_msg_nr_send.ntmn_failed,
	     console.ntcc_clients.ntcrc_sd->ntcsd_msg_nr_send.ntmn_bad);
	/* send STOP command to the test clients */
	rc = c2_net_test_console_cmd(&console, C2_NET_TEST_ROLE_CLIENT,
				     C2_NET_TEST_CMD_STOP);
	C2_UT_ASSERT(rc == clients_nr);
	/* send STOP command to the test servers */
	rc = c2_net_test_console_cmd(&console, C2_NET_TEST_ROLE_SERVER,
				     C2_NET_TEST_CMD_STOP);
	C2_UT_ASSERT(rc == servers_nr);
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
			       2, 2, 1, 8, 8, 0x100);
	//net_test_client_server("0@lo", C2_NET_TEST_TYPE_PING,
	//		       8, 8, 4, 16, 0x100, 0x100);
}

void c2_net_test_client_server_bulk_ut(void)
{
	/*
	net_test_client_server("0@lo", C2_NET_TEST_TYPE_BULK,
			       8, 8, 4, 16, 0x100, 0x10000);
	*/
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
