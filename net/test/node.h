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
     - @ref net-test-fspec-cli-node-kernel
     - @subpage net-test-fspec-cli-node-user "Userspace node"
     - @subpage net-test-fspec-cli-console "Console"
   - @ref net-test-fspec-usecases
     - @ref net-test-fspec-usecases-kernel
     - @ref net-test-fspec-usecases-console "Console"
   - @ref NetTestDFS "Detailed Functional Specification"
   - @ref NetTestInternals "Internals"

   @section net-test-fspec-ds Data Structures

   @see @ref NetTestDFS

   @section net-test-fspec-sub Subroutines
   @see @ref NetTestDFS

   @subsection net-test-fspec-sub-cons Constructors and Destructors

   @subsection net-test-fspec-sub-acc Accessors and Invariants

   @subsection net-test-fspec-sub-opi Operational Interfaces

   @section net-test-fspec-cli Command Usage

   @subsection net-test-fspec-cli-node-kernel Kernel module options

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
		 Standard suffixes (kKmMgG) can be added to the number.
   - @b target Test servers list for the test client and vice versa.
		   Items in list are comma-separated. Mandatory option.
		   Test clients list for server is used for preallocating
		   endpoint structures.
   - @b console Console hostname. Mandatory option.

   @section net-test-fspec-usecases Recipes

   @subsection net-test-fspec-usecases-kernel Kernel module parameters example

   @code
   node_role=client test_type=ping count=10 target=s1,s2,s3
   @endcode
   Run ping test as test client with 10 test messages to servers s1, s2 and s3.

   @code
   node_role=server test_type=bulk target=c1,c2 size=128k
   @endcode
   Run bulk test as test server with 128kB (=131072 bytes) bulk message size and
   test clients c1 and c2.

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
	/** Test service */
	struct c2_net_test_service    *ntnc_svc;
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
   @see @ref net-test-lspec
 */
int c2_net_test_node_start(struct c2_net_test_node_ctx *ctx);

/**
   Stop test node.
   @see @ref net-test-lspec
 */
void c2_net_test_node_stop(struct c2_net_test_node_ctx *ctx);

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
