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

/* @todo remove */
#ifndef __KERNEL__
#include <stdio.h>		/* printf */
#endif

/* @todo debug only, remove it */
#ifndef __KERNEL__
//#define LOGD(format, ...) printf(format, ##__VA_ARGS__)
#define LOGD(format, ...) do {} while (0)
#else
#define LOGD(format, ...) do {} while (0)
#endif

#include "lib/errno.h"		/* ETIMEDOUT */
#include "lib/misc.h"		/* C2_SET0 */

#include "net/test/node.h"	/* c2_net_test_node_ctx */
#include "net/test/node_ping.h"	/* c2_net_test_node_ping_ops */
#include "net/test/node_bulk.h"	/* c2_net_test_node_bulk_ops */

/**
   @page net-test Colibri Network Benchmark

   - @ref net-test-ovw
   - @ref net-test-def
   - @ref net-test-req
   - @ref net-test-depends
   - @ref net-test-highlights
   - @subpage net-test-fspec "Functional Specification"
   - @ref net-test-lspec
     - @ref net-test-lspec-comps
     - @ref net-test-lspec-state
     - @ref net-test-lspec-thread
     - @ref net-test-lspec-numa
   - @ref net-test-conformance
   - @ref net-test-ut
   - @ref net-test-st
   - @ref net-test-O
   - @ref net-test-ref


   <hr>
   @section net-test-ovw Overview

   Colibri network benchmark is designed to test network subsystem of Colibri
   and network connections between nodes that are running Colibri.

   Colibri Network Benchmark is implemented as a kernel module for test node
   and user space program for test console.
   Before testing kernel module must be copied to every test node.
   Then test console will perform test in this way:

   @msc
   console, node;
   console->node	[label = "Load kernel module"];
   node->console	[label = "Node is ready"];
   ---			[label = "waiting for all nodes"];
   console->node	[label = "Command to start test"];
   node rbox node	[label = "Executing test"];
   node->console	[label = "Statistics"];
   ---			[label = "waiting for all nodes"];
   console rbox console [label = "Print summary statistics"];
   console->node	[label = "Unload kernel module"];
   ---			[label = "waiting for all nodes"];
   @endmsc

   <hr>
   @section net-test-def Definitions

   Previously defined terms:
   @see @ref net-test-hld "Colibri Network Benchmark HLD"

   New terms:
   - <b>Configuration variable</b> Variable with name. It can have some value.
   - @b Configuration Set of name-value pairs.

   <hr>
   @section net-test-req Requirements

   - @b R.c2.net.self-test.statistics statistics from the all nodes
     can be collected on the test console.
   - @b R.c2.net.self-test.statistics.live statistics from the all nodes
     can be collected on the test console at any time during the test.
   - @b R.c2.net.self-test.statistics.live pdsh is used to perform
     statistics collecting from the all nodes with some interval.
   - @b R.c2.net.self-test.test.ping latency is automatically measured for
     all messages.
   - @b R.c2.net.self-test.test.bulk used messages with additional data.
   - @b R.c2.net.self-test.test.bulk.integrity.no-check bulk messages
      additional data isn't checked.
   - @b R.c2.net.self-test.test.duration.simple end user should be able to
     specify how long a test should run, by loop.
   - @b R.c2.net.self-test.kernel test client/server is implemented as
     a kernel module.

   <hr>
   @section net-test-depends Dependencies

   - R.c2.net

   <hr>
   @section net-test-highlights Design Highlights

   - c2_net is used as network library.
   - To make latency measurement error as little as possible all
     heavy operations (such as buffer allocation) will be done before
     test message exchanging between test client and test server.

   <hr>
   @section net-test-lspec Logical Specification

   - @ref net-test-lspec-comps
     - @ref net-test-lspec-ping
     - @ref net-test-lspec-bulk
     - @ref net-test-lspec-algo-client
     - @ref net-test-lspec-algo-server
     - @ref net-test-lspec-console
     - @ref net-test-lspec-misc
   - @ref net-test-lspec-state
   - @ref net-test-lspec-thread
   - @ref net-test-lspec-numa

   @subsection net-test-lspec-comps Component Overview

   @dot
   digraph {
     node [style=box];
     label = "Network Benchmark Source File Relationship";
     nodeU    [label="user_space/node.c"];
     nodeK    [label="linux_kernel/main.c"];
     console  [label="user_space/console.c"];
     server   [label="server.c"];
     client   [label="client.c"];
     config   [label="node_config.c"];
     main     [label="node_main.c"];
     network  [label="network.c"];
     stats    [label="stats.c"];
     commands [label="commands.c"];
     nodeU    -> main;
     nodeK    -> main;
     main     -> client;
     main     -> server;
     client   -> network;
     client   -> stats;
     client   -> config;
     client   -> commands;
     server   -> network;
     server   -> stats;
     server   -> config;
     server   -> commands;
     console  -> network;
     console  -> stats;
     console  -> commands;
     commands -> network;
   }
   @enddot

   Test client/server (PROG) can be run in such a way:

   @code
   int rc;
   // wait until INIT command received
   rc = c2_net_test_PROG_init();
   if (rc == 0) {
	// send INIT DONE command
	// wait until START command received
	rc = c2_net_test_PROG_start();
	// send FINISHED command (if PROG == client)
	if (rc == 0)
		c2_net_test_PROG_stop();
   }
   // wait for FINISHED ACK response (if PROG == client)
   // send FINISHED ACK response (if PROG == server)
   c2_net_test_PROG_fini();
   @endcode

   c2_net_test_PROG_start() will be blocked until it's finished or
   c2_net_test_PROG_stop() called. Test server needs to catch
   FINISHED command from the test console and run c2_net_test_server_stop()
   when it is received.

   @subsubsection net-test-lspec-ping Ping Test

   One test message travel:
   @msc
   c [label = "Test Client"],
   s [label = "Test Server"];

   |||;
   c rbox c [label = "Create test message for ping test with timestamp
	              and sequence number"];
   c=>s     [label = "Test message"];
   ...;
   s=>c     [label = "Test message"];
   c rbox c [label = "Check test message timestamp and sequence number,
		     add to statistics"];
   |||;
   @endmsc

   @subsubsection net-test-lspec-bulk Bulk Test

   One test message travel:
   @msc
   c [label = "Test Client"],
   s [label = "Test Server"];

   |||;
   c rbox c [label = "Allocate buffers for passive send/receive"];
   c rbox c [label = "Send buffer descriptors to the test server"];
   c=>s     [label = "Network buffer descriptors"];
   s rbox s [label = "Receive buffer descriptors from the test client"];
   c rbox c [label = "Start passive bulk sending"],
   s rbox s [label = "Start active bulk receiving"];
   c=>s	    [label = "Bulk data"];
   ...;
   |||;
   c rbox c [label = "Finish passive bulk sending"],
   s rbox s [label = "Finish active bulk receiving"];
   s rbox s [label = "Initialize bulk transfer as an active bulk sender"];
   c rbox c [label = "Start passive bulk receiving"],
   s rbox s [label = "Start active bulk sending"];
   s=>c     [label = "Bulk data"];
   ...;
   |||;
   c rbox c [label = "Finish passive bulk receiving"],
   s rbox s [label = "Finish active bulk sending"];
   |||;
   @endmsc

   @subsubsection net-test-lspec-algo-client Test Client Algorithm

   @dot
   digraph {
     S0 [label="entry point"];
     S1 [label="msg_left = msg_count;\l\
semaphore buf_free = concurrency;\l", shape=box];
     SD [label="", height=0, width=0, shape=plaintext];
     S2 [label="msg_left > 0?", shape=diamond];
     S3 [label="buf_free.down();", shape=box];
     S4 [label="stop.trydown()?", shape=diamond];
     S5 [label="msg_left--;", shape=box];
     S6 [label="test_type == ping?", shape=diamond];
     SA [label="add recv buf to recv queue\l\
add send buf to send queue", shape=box];
     SB [label="add bulk buf to PASSIVE_SEND queue\l\
add bulk buf to PASSIVE_RECV queue\l\
add 2 buf descriptors to MSG_SEND queue\l", shape=box];
     SC [label="", height=0, width=0, shape=plaintext];
     S7 [label="stop.down();\lbuf_free.up();\l", style=box];
     S8 [label="for (i = 0; i < concurrency; ++i)\l  buf_free.down();\l\
finished.up();\l", shape=box];
     S9 [label="exit point"];
     S0   -> S1;
     S1:s -> SD;
     SD   -> S2:n;
     S2:e -> S3   [label="yes"];
     S2:w -> S8   [label="no"];
     S3:s -> S4:n;
     S4:e -> S5   [label="no"];
     S4:w -> S7   [label="yes"];
     S5   -> S6;
     S6:e -> SA:n [label="yes"];
     S6:w -> SB:n [label="no"];
     SA:s -> SC;
     SB:s -> SC;
     SC   -> SD;
     S7   -> S8;
     S8   -> S9;
   }
   @enddot

   Callbacks for ping test
   - C2_NET_QT_MSG_SEND
     - update stats
   - C2_NET_QT_MSG_RECV
     - update stats
     - buf_free.up()

   Callbacks for bulk test
   - C2_NET_QT_MSG_SEND
     - nothing
   - C2_NET_QT_PASSIVE_BULK_SEND
     - update stats
   - C2_NET_QT_PASSIVE_BULK_RECV
     - update stats
     - buf_free.up()

   External variables and interrupts
   - semaphore stop = 0;
   - semaphore finished = 0;
   - client_stop()
     - stop.up()
   - stop and block until finish
     - client_stop()
     - finished.down()

   @subsubsection net-test-lspec-algo-server Test Server Algorithm

   Test server allocates all necessary buffers and initializes transfer
   machine. Then it just works in transfer machine callbacks.

   Ping test callbacks
   - C2_NET_QT_MSG_RECV
     - add buffer to msg send queue
     - update stats
   - C2_NET_QT_MSG_SEND
     - add buffer to msg recv queue

   Bulk test callbacks
   - C2_NET_QT_MSG_RECV
     - add first buffer descriptor to ACTIVE_BULK_RECV queue
     - add msg buffer to MSG_RECV queue
   - C2_NET_QT_ACTIVE_BULK_RECV
     - add second buffer descriptor to ACTIVE_BULK_SEND queue
     (to send just received buffer)
   - C2_NET_QT_ACTIVE_BULK_SEND
     - add sent buffer to ACTIVE_BULK_RECV queue

   @subsubsection net-test-lspec-console Test Console
   @msc
   console, client, server;

   |||;
   client rbox client [label = "Listening for console commands"],
   server rbox server [label = "Listening for console commands"];
   console => server  [label = "INIT command"];
   server => console  [label = "INIT DONE response"];
   ---                [label = "waiting for all servers"];
   console => client  [label = "INIT command"];
   client => console  [label = "INIT DONE response"];
   ---                [label = "waiting for all clients"];
   console => server  [label = "START command"];
   server => console  [label = "START ACK response"];
   ---                [label = "waiting for all servers"];
   console => client  [label = "START command"];
   client => console  [label = "START ACK response"];
   ---                [label = "waiting for all clients"];
   ---                [label = "running..."];
   console => client  [label = "STATUS command"];
   client => console  [label = "STATUS DATA response"];
   console => server  [label = "STATUS command"];
   server => console  [label = "STATUS DATA response"];
   ---                [label = "console wants to stop client&server"];
   console => client  [label = "STOP command"];
   client => console  [label = "STOP DONE response"];
   client rbox client [label = "clients cleanup"];
   ---                [label = "waiting for all clients"];
   console => server  [label = "STOP command"];
   server => console  [label = "STOP DONE response"];
   server rbox server [label = "server cleanup"];
   ---                [label = "waiting for all servers"];
   @endmsc

   @subsubsection net-test-lspec-misc Misc
   - Typed variables are used to store configuration.
   - Configuration variables are set in c2_net_test_config_init(). They
   should be never changed in other place.
   - c2_net_test_stats is used for keeping some data for sample,
   based on which min/max/average/standard deviation can be calculated.
   - c2_net_test_network_init()/c2_net_test_network_fini() need to be called to
   initialize/finalize c2_net_test network.
   - c2_net_test_network_(msg/bulk)_(send/recv)_* is a wrapper around c2_net.
   This functions use c2_net_test_ctx as containter for buffers, callbacks,
   endpoints and transfer machine. Buffer/endpoint index (int in range
   [0, NR), where NR is number of corresponding elements) is used for selecting
   buffer/endpoint structure from c2_net_test_ctx.
   - All buffers are allocated in c2_net_test_network_ctx_init().
   - Endpoints can be added after c2_net_test_network_ctx_init() using
   c2_net_test_network_ep_add().

   @subsection net-test-lspec-state State Specification

   @dot
   digraph {
     node [style=box];
     label = "Test Client and Test Server States";
     S0 [label="Uninitialized"];
     S1 [label="Ready"];
     S2 [label="Finished"];
     S3 [label="Failed"];
     S0 -> S1 [label="succesful c2_net_test_service_init()"];
     S1 -> S0 [label="c2_net_test_service_fini()"];
     S1 -> S2 [label="service state change: service was finished"];
     S1 -> S3 [label="service state change: service was failed"];
     S2 -> S0 [label="c2_net_test_service_fini()"];
     S3 -> S0 [label="c2_net_test_service_fini()"];
   }
   @enddot

   @subsection net-test-lspec-thread Threading and Concurrency Model

   - Configuration is not protected by any synchronization mechanism.
     Configuration is not intended to change after initialization,
     so no need to use synchronization mechanism for reading configuration.
   - struct c2_net_test_stats is not protected by any synchronization mechanism.
   - struct c2_net_test_ctx is not protected by any synchronization mechanism.

   @subsection net-test-lspec-numa NUMA optimizations

   - Configuration is not intended to change after initial initialization,
     so cache coherence overhead will not exists.
   - One c2_net_test_stats per locality can be used. Summary statistics can
     be collected from all localities using c2_net_test_stats_add_stats()
     only when it needed.
   - One c2_net_test_ctx per locality can be used.

   <hr>
   @section net-test-conformance Conformance

   - @b I.c2.net.self-test.statistics user-space LNet implementation is used
     to collect statistics from all nodes.
   - @b I.c2.net.self-test.statistics.live user-space LNet implementation
     is used to perform statistics collecting from the all nodes with
     some interval.
   - @b I.c2.net.self-test.test.ping latency is automatically measured for
     all messages.
   - @b I.c2.net.self-test.test.bulk used messages with additional data.
   - @b I.c2.net.self-test.test.bulk.integrity.no-check bulk messages
      additional data isn't checked.
   - @b I.c2.net.self-test.test.duration.simple end user is able to
     specify how long a test should run, by loop - see
     @ref net-test-fspec-cli-console.
   - @b I.c2.net.self-test.kernel test client/server is implemented as
     a kernel module.

   <hr>
   @section net-test-ut Unit Tests

   @test Ping message send/recv over loopback device.
   @test Concurrent ping messages send/recv over loopback device.
   @test Bulk active send/passive receive over loopback device.
   @test Bulk passive send/active receive over loopback device.
   @test Statistics for sample with one value.
   @test Statistics for sample with ten values.
   @test Merge two c2_net_test_stats structures with
	 c2_net_test_stats_add_stats()

   <hr>
   @section net-test-st System Tests

   @test Script for network benchmark ping/bulk self-testing over loopback
	 device on single node.
   @test Script for tool ping/bulk testing with two test nodes.

   <hr>
   @section net-test-O Analysis

   - all c2_net_test_stats_* functions have O(1) complexity;
   - one mutex lock/unlock per statistics update in test client/server/console;
   - one semaphore up/down per test message in test client;

   @see @ref net-test-hld "Colibri Network Benchmark HLD"

   <hr>
   @section net-test-ref References

   @anchor net-test-hld
   - <a href="https://docs.google.com/a/xyratex.com/document/
view?id=1_dAYA4_5rLEr9Z4PSkH1OTDTMlGBmAcIEEJRvwCRDV4">
Colibri Network Benchmark HLD</a>
   - <a href="http://wiki.lustre.org/manual/LustreManual20_HTML/
LNETSelfTest.html">LNET Self-Test manual</a>
   - <a href="http://reviewboard.clusterstor.com/r/773">DLD review request</a>

 */

/**
   @defgroup NetTestInternals Internals
   @ingroup NetTestDFS

   @see @ref net-test
 */

/**
   @defgroup NetTestNodeInternals Test Node Internals
   @ingroup NetTestInternals

   @see @ref net-test

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

	switch (cmd->ntc_init.ntci_type) {
	case C2_NET_TEST_TYPE_PING:
		return &c2_net_test_node_ping_ops;
	case C2_NET_TEST_TYPE_BULK:
		return &c2_net_test_node_bulk_ops;
	default:
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
	if (rc == 0) {
		LOGD("node_cmd_get: rc = %d\n", rc);
		LOGD("node_cmd_get: cmd->ntc_type = %d\n", cmd->ntc_type);
		LOGD("node_cmd_get: cmd->ntc_init.ntci_msg_nr = %lu\n",
		     cmd->ntc_init.ntci_msg_nr);
	}
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
	} while (!(rc == 0 && cmd->ntc_type == type) && !ctx->ntnc_exit_flag);
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
	bool				skip_cmd_get;

	C2_PRE(ctx != NULL);

	/* wait for INIT command */
	C2_SET0(&cmd);
	ctx->ntnc_errno = node_cmd_wait(ctx, &cmd, C2_NET_TEST_CMD_INIT);
	if (ctx->ntnc_exit_flag) {
		c2_net_test_commands_received_free(&cmd);
		return;
	}
	if (ctx->ntnc_errno != 0)
		return;
	/* we have configuration; initialize test service */
	svc_ops = service_ops_get(&cmd);
	if (svc_ops == NULL) {
		c2_net_test_commands_received_free(&cmd);
		return;
	}
	rc = c2_net_test_service_init(&svc, svc_ops);
	if (rc != 0) {
		c2_net_test_commands_received_free(&cmd);
		return;
	}
	/* handle INIT command inside main loop */
	skip_cmd_get = true;
	/* test service is initialized. start main loop */
	do {
		/* get command */
		if (rc == 0 && !skip_cmd_get)
			rc = node_cmd_get(&ctx->ntnc_cmd, &cmd, c2_time_now());
		else
			skip_cmd_get = false;
		if (rc == 0)
			LOGD("node_thread: cmd_get, "
			     "rc = %d, cmd.ntc_ep_index = %lu\n",
			     rc, cmd.ntc_ep_index);
		if (rc == 0 && cmd.ntc_ep_index >= 0) {
			LOGD("node_thread: have command\n");
			/* we have command. handle it */
			rc = c2_net_test_service_cmd_handle(&svc, &cmd, &reply);
			LOGD("node_thread: cmd handle: rc = %d\n", rc);
			reply.ntc_ep_index = cmd.ntc_ep_index;
			c2_net_test_commands_received_free(&cmd);
			/* send reply */
			LOGD("node_thread: reply.ntc_ep_index = %lu\n",
			     reply.ntc_ep_index);
			c2_net_test_commands_send_wait_all(&ctx->ntnc_cmd);
			rc = c2_net_test_commands_send(&ctx->ntnc_cmd, &reply);
			LOGD("node_thread: send reply: rc = %d\n", rc);
			C2_SET0(&cmd);
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

	LOGD("6, rc = %d\n", rc);
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

	C2_SET0(ctx);

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

/**
   @} end of NetTestNodeInternals group
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
