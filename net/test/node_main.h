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

#ifndef __NET_TEST_NODE_MAIN_H__
#define __NET_TEST_NODE_MAIN_H__

/**
   @page net-test-fspec Functional Specification
   <i>Mandatory. This page describes the external interfaces of the
   component. The section has mandatory sub-divisions created using the Doxygen
   @@section command.  It is required that there be Table of Contents at the
   top of the page that illustrates the sectioning of the page.</i>

   - @ref net-test-fspec-ds
   - @ref net-test-fspec-sub
   - @ref net-test-fspec-cli
     - @ref net-test-fspec-cli-node "Linux kernel module"
     - @ref net-test-fspec-cli-console "Console"
   - @ref net-test-fspec-usecases
   - @subpage NetTestDFS
     - @subpage NetTestStatsDFS "Statistics Collector"
     - @subpage NetTestConfigDFS "Configuration"
     - @subpage NetTestClientDFS "Test Client"
     - @subpage NetTestServerDFS "Test Server"
     - @subpage NetTestNetworkDFS "Network"
     - @subpage NetTestCommandsDFS "Commands"
   - @subpage NetTestInternals "Internals"
     - @subpage NetTestStatsInternals "Statistics Collector"
     - @subpage NetTestConfigInternals "Configuration"
     - @subpage NetTestClientInternals "Test Client"
     - @subpage NetTestServerInternals "Test Server"
     - @subpage NetTestNetworkInternals "Network"
     - @subpage NetTestCommandsInternals "Commands"

   @section net-test-fspec-ds Data Structures
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and <i>brief</i> description of the
   major externally visible data structures defined by this component.  No
   details of the data structure are required here, just the salient
   points.</i>

   - c2_net_test_stats
   - c2_net_test_ctx

   @section net-test-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

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
   <i>Mandatory for command line programs.  Components that provide programs
   would provide a specification of the command line invocation arguments.  In
   addition, the format of any any structured file consumed or produced by the
   interface must be described in this section.</i>

   @subsection net-test-fspec-cli-node \
	   Command line options for the test client/server kernel module.

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
   - @b target Test servers list for the test client and vice versa.
		   Items in list are comma-separated. Mandatory option.
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
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>

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
   --type=bulk --clients=host1 --servers=host2 --count=1000000 --size=1048576
   --remote-path=$HOME/net-test --live=1
   @endcode
   Run bulk test with host1 as test client and host2 as test server. Number of
   bulk packets is one million, size is 1 MiB. Test statistics should be updated
   every second.

   @see @ref net-test
 */

#include "net/test/node_config.h"

/**
   @defgroup NetTestDFS Detailed Functional Specification
   @brief Detailed functional specification for Colibri Network Benchmark.

   @see @ref net-test

   @{
 */

/**
   Start c2_net_test on the test node.
   It will determine whether it is a client or a server and
   will run corresponding subroutine.
   @todo implement as service
 */
int c2_net_test_init(struct c2_net_test_node_config *cfg);

/**
   Stop c2_net_test on the test node.
   Will interrupt all running tests.
   Will block until all tests stopped.
 */
void c2_net_test_fini(void);

/**
   @} end of NetTestDFS
 */

#endif /*  __NET_TEST_MAIN_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
