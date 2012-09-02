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

#pragma once

#ifndef __NET_TEST_NODE_H__
#define __NET_TEST_NODE_H__

#include "net/test/commands.h"	/* c2_net_test_cmd_ctx */
#include "net/test/network.h"	/* c2_net_test_network_ctx */

/**
   @page net-test-fspec Functional Specification

   - @ref net-test-fspec-ds
   - @ref net-test-fspec-sub
   - @ref net-test-fspec-cli
     - @ref net-test-fspec-cli-node "Linux kernel module"
     - @ref net-test-fspec-cli-console "Console"
   - @ref net-test-fspec-usecases
   - @ref NetTestDFS "Detailed Functional Specification"
   - @ref NetTestInternals "Internals"

   @section net-test-fspec-ds Data Structures

   - c2_net_test_stats
   - c2_net_test_ctx

   @todo update this section
   @section net-test-fspec-sub Subroutines
   @todo update this section

   @subsection net-test-fspec-sub-cons Constructors and Destructors

   - c2_net_test_main()
   - c2_net_test_config_init()
   - c2_net_test_config_fini()
   - c2_net_test_stats_init()
   - c2_net_test_stats_init_zero()
   - c2_net_test_stats_fini()
   - c2_net_test_client_init()
   - c2_net_test_client_fini()
   - c2_net_test_server_init()
   - c2_net_test_server_fini()
   - c2_net_test_network_init()
   - c2_net_test_network_fini()
   - c2_net_test_network_ctx_init()
   - c2_net_test_network_ctx_fini()

   @subsection net-test-fspec-sub-acc Accessors and Invariants

   - c2_net_test_config_invariant()

   @subsection net-test-fspec-sub-opi Operational Interfaces

   - c2_net_test_stats_add()
   - c2_net_test_stats_add_stats()
   - c2_net_test_stats_min()
   - c2_net_test_stats_max()
   - c2_net_test_stats_avg()
   - c2_net_test_stats_stddev()
   - c2_net_test_stats_count()

   - c2_net_test_client_start()
   - c2_net_test_client_stop()
   - c2_net_test_server_start()
   - c2_net_test_server_stop()

   - c2_net_test_network_ep_add()
   - c2_net_test_network_msg_send()
   - c2_net_test_network_msg_recv()
   - c2_net_test_network_bulk_send_passive()
   - c2_net_test_network_bulk_recv_passive()
   - c2_net_test_network_bulk_send_active()
   - c2_net_test_network_bulk_recv_active()

   - c2_net_test_commands_send()
   - c2_net_test_commands_wait()

   @section net-test-fspec-cli Command Usage

   @subsection net-test-fspec-cli-node \
	   Kernel module options for the test client/server kernel module.

   - @b node_role Node role. Mandatory option.
     - @b client Program will act as test client.
     - @b server Program will act as test server.
   - @b test_type Test type. Mandatory option.
     - @b ping Ping test will be executed.
     - @b bulk Bulk test will be executed.
   - @b count Number of test messages to exchange between test client
                  and test server. Makes sense for test client only.
		  Default value is 16.
   - @b size Size of bulk messages, bytes. Makes sense for bulk test only.
	         Default value is 1048576 (1Mb).
		 Prefixes K (for kilobyte), M (for megabyte) and
                 G (for gigabyte) can be used here.
   - @b target Test servers list for the test client and vice versa.
		   Items in list are comma-separated. Mandatory option.
		   Test clients list for server is used for preallocating
		   endpoint structures.
   - @b console Console hostname. Mandatory option.

   @subsection net-test-fspec-cli-console \
	   Command line options for the test console.

   Installing/uninstalling test suite (kernel modules, scripts etc.)
   to/from remote host:
   - @b --install Install test suite. This means only copying binaries,
                  scripts etc., but not running something.
   - @b --uninstall Uninstall test suite.
   - @b --remote-path Remote path for installing.
   - @b --targets Comma-separated list of host names for installion.

   Running test:
   - @b --type Test type. Can be @b bulk or @b ping.
   - @b --clients Comma-separated list of test client hostnames.
   - @b --servers Comma-separated list of test server hostnames.
   - @b --count Number of test messages to exchange between every test
		client and every test server.
   - @b --size Size of bulk messages, bytes. Makes sense for bulk test only.
   - @b --remote-path Path to test suite on remote host.
   - @b --live Live report update time, seconds.

   @section net-test-fspec-usecases Recipes

   @subsection net-test-fspec-usecases-kernel Kernel module parameters example

   @code
   node_role=client test_type=ping count=10 target=s1,s2,s3
   @endcode
   Run ping test as test client with 10 test messages to servers s1, s2 and s3.

   @code
   node_role=server test_type=bulk target=c1,c2
   @endcode
   Run bulk test as test server with 1Mb bulk message size and
   test clients c1 and c2.

   @subsection net-test-fspec-usecases-console Test console parameters example

   @code
   --install --remote-path=$HOME/net-test --targets=c1,c2,c3,s1,s2
   @endcode
   Install test suite to $HOME/net-test directory on hosts c1, c2, c3,
   s1 and s2.

   @code
   --uninstall --remote-path=/tmp/net-test --targets=host1,host2
   @endcode
   Uninstall test suite on hosts host1 and host2.

   @code
   --type=ping --clients=c1,c2,c3 --servers=s1,s2 --count=1024
   --remote-path=$HOME/net-test
   @endcode
   Run ping test with hosts c1, c2 and c3 as clients and s2 and s2 as servers.
   Ping test should have 1024 test messages and test suite on remote hosts
   is installed in $HOME/net-test.

   @code
   --type=bulk --clients=host1 --servers=host2 --count=1000000 --size=1M
   --remote-path=$HOME/net-test --live=1
   @endcode
   Run bulk test with host1 as test client and host2 as test server. Number of
   bulk packets is one million, size is 1 MiB. Test statistics should be updated
   every second.

   @see @ref net-test
 */

/**
   @defgroup NetTestDFS Network Benchmark
   @brief Detailed functional specification for Colibri Network Benchmark.

   @see @ref net-test
 */

/**
   @defgroup NetTestNodeDFS Test Node
   @ingroup NetTestDFS

   @see @ref net-test

   @{
 */

/** Node state */
enum c2_net_test_node_state {
	C2_NET_TEST_NODE_UNINITIALIZED,
	C2_NET_TEST_NODE_INITIALIZED,
	C2_NET_TEST_NODE_RUNNING,
	C2_NET_TEST_NODE_FAILED,
	C2_NET_TEST_NODE_DONE,
	C2_NET_TEST_NODE_STOPPED,
};

/** Node configuration */
struct c2_net_test_node_cfg {
	/** Node endpoint address (for commands) */
	char	 *ntnc_addr;
	/** Console endpoint address (for commands) */
	char	 *ntnc_addr_console;
	/** Send commands timeout. @see c2_net_test_commands_init(). */
	c2_time_t ntnc_send_timeout;
};

/** Node context. */
struct c2_net_test_node_ctx {
	/** Commands context. Connected to the test console. */
	struct c2_net_test_cmd_ctx     ntnc_cmd;
	/** Network context for testing */
	struct c2_net_test_network_ctx ntnc_net;
	/** Test service */
	struct c2_net_test_service    *ntnc_svc;
	/** Service private data. Set and used in service implementations. */
	void			      *ntnc_svc_private;
	/** Node thread */
	struct c2_thread	       ntnc_thread;
	/**
	   Exit flag for the node thread.
	   Node thread will check this flag and will terminate if it is set.
	 */
	bool			       ntnc_exit_flag;
	/** Error code. Set in node thread if something goes wrong. */
	int			       ntnc_errno;
	/**
	 * 'node-thread-was-finished' semaphore.
	 * Initialized to 0. External routines can down() or timeddown()
	 * this semaphore to wait for the node thread.
	 * up() at the end of the node thread.
	 */
	struct c2_semaphore	       ntnc_thread_finished_sem;
};

/**
   Initialize node data structures.
   @param ctx node context.
   @param cfg node configuration.
   @see @ref net-test-lspec
   @note ctx->ntnc_cfg should be set and should not be changed after
   c2_net_test_node_init().
 */
int c2_net_test_node_init(struct c2_net_test_node_ctx *ctx,
			  struct c2_net_test_node_cfg *cfg);

/**
   Finalize node data structures.
   @see @ref net-test-lspec
 */
void c2_net_test_node_fini(struct c2_net_test_node_ctx *ctx);

/**
   Invariant for c2_net_test_node_ctx.
 */
bool c2_net_test_node_invariant(struct c2_net_test_node_ctx *ctx);

/**
   Start test node.
   This function will return only after test node finished or interrupted
   with c2_net_test_node_stop().
   @see @ref net-test-lspec
 */
int c2_net_test_node_start(struct c2_net_test_node_ctx *ctx);

/**
   Stop test node.
   @see @ref net-test-lspec
 */
void c2_net_test_node_stop(struct c2_net_test_node_ctx *ctx);

/**
   Get c2_net_test_node_ctx from c2_net_test_network_ctx.
   Useful in the network buffer callbacks.
 */
struct c2_net_test_node_ctx
*c2_net_test_node_ctx_from_net_ctx(struct c2_net_test_network_ctx *net_ctx);

/**
   @} end of NetTestNodeDFS group
 */

#endif /*  __NET_TEST_NODE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
