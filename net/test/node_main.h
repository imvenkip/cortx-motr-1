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

#ifndef __NET_TEST_MAIN_H__
#define __NET_TEST_MAIN_H__

/**
   @page net-test-fspec Functional Specification
   <i>Mandatory. This page describes the external interfaces of the
   component. The section has mandatory sub-divisions created using the Doxygen
   @@section command.  It is required that there be Table of Contents at the
   top of the page that illustrates the sectioning of the page.</i>

   - @ref net-test-fspec-ds
   - @ref net-test-fspec-sub
   - @ref net-test-fspec-cli
   - @ref net-test-fspec-usecases
   - @subpage NetTestDFS
     - @subpage NetTestStatsDFS "Statistics Collector"
     - @subpage NetTestConfigDFS "Configuration"
     - @subpage NetTestClientDFS "Test Client"
     - @subpage NetTestServerDFS "Test Server"
     - @subpage NetTestNetworkDFS "Network"
     - @subpage NetTestConsoleDFS "Console"
   - @subpage NetTestInternals "Internals"
     - @subpage NetTestStatsInternals "Statistics Collector"
     - @subpage NetTestConfigInternals "Configuration"
     - @subpage NetTestClientInternals "Test Client"
     - @subpage NetTestServerInternals "Test Server"
     - @subpage NetTestNetworkInternals "Network"
     - @subpage NetTestConsoleInternals "Console"

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

   @section net-test-fspec-cli Command Usage
   <i>Mandatory for command line programs.  Components that provide programs
   would provide a specification of the command line invocation arguments.  In
   addition, the format of any any structured file consumed or produced by the
   interface must be described in this section.</i>

   This command line options are valid for both client and server testing role.

   - <b>node_role</b> Node role. Mandatory option.
     - <b>client</b> Program will act as test client.
     - <b>server</b> Program will act as test server.
   - <b>test_type</b> Test type. Mandatory option.
     - <b>ping</b> Ping test will be executed.
     - <b>bulk</b> Bulk test will be executed.
   - <b>count</b> Number of test messages to exchange between test client
                  and test server. Makes sense for test client only.
		  Default value is 16.
   - <b>size</b> Size of bulk messages, bytes. Makes sense for bulk test only.
	         Default value is 1048576 (1Mb).
   - <b>target</b> Test servers list for the test client and vice versa.
		   Items in list are comma-separated. Mandatory option.
   - <b>console</b> Console hostname. Mandatory option.

   @section net-test-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>

   node_role=client test_type=ping count=10 target=s1,s2,s3 @n
   Run ping test as test client with 10 test messages to servers s1, s2 and s3.
   @n @n
   node_role=server test_type=bulk target=c1,c2 @n
   Run bulk test as test server with 1Mb bulk message size and
   test clients c1 and c2.

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
