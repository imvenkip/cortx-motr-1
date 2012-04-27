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
   <i>All specifications must start with an Overview section that
   briefly describes the document and provides any additional
   instructions or hints on how to best read the specification.</i>

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
   <i>Mandatory.
   The DLD shall provide definitions of the terms and concepts
   introduced by the design, as well as the relevant terms used by the
   specification but described elsewhere.  References to the
   C2 Glossary are permitted and encouraged.  Agreed upon terminology
   should be incorporated in the glossary.</i>

   Previously defined terms:
   @see @ref net-test-hld "Colibri Network Benchmark HLD"

   New terms:
   - <b>Configuration variable</b> Variable with name. It can have some value.
   - <b>Configuration</b> Set of name-value pairs.

   <hr>
   @section net-test-req Requirements
   <i>Mandatory.
   The DLD shall state the requirements that it attempts to meet.</i>

   - <b>R.c2.net.self-test.statistics</b> pdsh is used to gather the statistics
     from the all nodes.
   - <b>R.c2.net.self-test.statistics.live</b> pdsh is used to perform
     statistics collecting from the all nodes with some interval.
   - <b>R.c2.net.self-test.test.ping</b> latency is automatically measured for
     all messages.
   - <b>R.c2.net.self-test.test.bulk</b> used messages with additional data.
   - <b>R.c2.net.self-test.test.bulk.integrity.no-check</b> bulk messages
      additional data isn't checked.
   - <b>R.c2.net.self-test.test.duration.simple</b> end user should be able to
     specify how long a test should run, by loop.
   - <b>R.c2.net.self-test.kernel</b> test client/server is implemented as
     a kernel module.

   <hr>
   @section net-test-depends Dependencies
   <i>Mandatory. Identify other components on which this specification
   depends.</i>

   <hr>
   @section net-test-highlights Design Highlights
   <i>Mandatory. This section briefly summarizes the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>
   @todo

   <hr>
   @section net-test-lspec Logical Specification
   <i>Mandatory.  This section describes the internal design of the component,
   explaining how the functional specification is met.  Sub-components and
   diagrams of their interaction should go into this section.  The section has
   mandatory subsections created using the Doxygen @@subsection command.  The
   designer should feel free to use additional sub-sectioning if needed, though
   if there is significant additional sub-sectioning, provide a table of
   contents here.</i>

   - @ref net-test-lspec-comps
     - @ref net-test-lspec-ping
     - @ref net-test-lspec-bulk
     - @ref net-test-lspec-config
     - @ref net-test-lspec-stats
     - @ref net-test-lspec-network
     - @ref net-test-lspec-console
   - @ref net-test-lspec-state
   - @ref net-test-lspec-thread
     - @ref net-test-lspec-thread-config
     - @ref net-test-lspec-thread-stats
     - @ref net-test-lspec-thread-network
   - @ref net-test-lspec-numa
     - @ref net-test-lspec-numa-config
     - @ref net-test-lspec-numa-stats
     - @ref net-test-lspec-numa-network

   @subsection net-test-lspec-comps Component Overview
   <i>Mandatory.
   This section describes the internal logical decomposition.
   A diagram of the interaction between internal components and
   between external consumers and the internal components is useful.</i>

   @dot
   digraph {
     node [style=box];
     label = "Network Benchmark Source File Relationship";
     subgraph kernel_main_sg {
       kernel_main [label = "linux_kernel/main.c"];
     }
     subgraph programs {
       node_main [label = "node_main.c"];
       console	 [label = "console.c"];
     }
     subgraph client_server {
       client [label = "client.c"];
       server [label = "server.c"];
     }
     subgraph libraries {
       stats	   [label="stats.c"];
       network     [label="network.c"];
       node_config [label="node_config.c"];
     }
     subgraph kernel_config_sg {
       kernel_config [label = "linux_kernel/node_config_k.c"];
     }
     kernel_main -> node_main;
     node_main	 -> client;
     node_main	 -> server;
     client	 -> stats;
     client	 -> network;
     client	 -> node_config;
     server	 -> stats;
     server	 -> network;
     server	 -> node_config;
     node_config -> kernel_config;
     console	 -> stats;
   }
   @enddot

   Test client/server/console (PROG) can be run in such a way:

   @code
   int rc;
   rc = c2_net_test_PROG_init();
   if (rc == 0) {
	rc = c2_net_test_PROG_start();
	if (rc == 0)
		c2_net_test_PROG_stop();
   }
   c2_net_test_PROG_fini();
   @endcode
   c2_net_test_PROG_stop() can be used to interrupt c2_net_test_PROG_start().

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

   @subsubsection net-test-lspec-config Configuration

   - Typed variables are used to store configuration.
   - Configuration variables are set in c2_net_test_config_init(). They
   should be never changed in other place.

   @subsubsection net-test-lspec-stats Statistics
   - c2_net_test_stats is used for keeping some data for sample,
   based on which min/max/average/standard deviation can be calculated.

   @subsubsection net-test-lspec-network Network
   - c2_net_test_network_init()/c2_net_test_network_fini() need to be called to
   initialize/finalize c2_net_test network.
   - c2_net_test_network_(msg/bulk)_(send/rev)_* is a wrapper around c2_net.
   This functions use c2_net_test_ctx as containter for buffers, callbacks,
   endpoints and transfer machine. Buffer/endpoint index (int in range
   [0, NR), where NR is number of corresponding elements) is used for selecting
   buffer/endpoint structure from c2_net_test_ctx.
   - All buffers are allocated in c2_net_test_network_ctx_init().
   - Endpoints can be added after c2_net_test_network_ctx_init() using
   c2_net_test_network_ep_add().

   @subsubsection net-test-lspec-console Test Console
   @todo add

   <hr>
   @subsection net-test-lspec-state State Specification
   <i>Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.</i>

   <hr>
   @subsection net-test-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   @subsubsection net-test-lspec-thread-config Configuration

   Configuration is not protected by any synchronization mechanism.
   Configuration is not intended to change after initialization,
   so no need to use synchronization mechanish for reading configuration.

   @subsubsection net-test-lspec-thread-stats Statistics

   struct c2_net_test_stats is not protected by any synchronization mechanism.

   @subsubsection net-test-lspec-thread-network Network

   struct c2_net_test_ctx is not protected by any synchronization mechanism.

   <hr>
   @subsection net-test-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   @subsubsection net-test-lspec-numa-config Configuration
   Configuration is not intented to change after initial initialization,
   so cache coherence overhead will not exists.

   @subsubsection net-test-lspec-numa-stats Statistics
   One c2_net_test_stats per locality can be used. Summary statistics can
   be collected from all localities using c2_net_test_stats_add_stats()
   only when it needed.

   @subsubsection net-test-lspec-numa-network Network
   One c2_net_test_ctx per locality can be used.

   <hr>
   @section net-test-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref net-test-req section,
   and explains briefly how the DLD meets the requirement.</i>

   <hr>
   @section net-test-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>

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
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   @test Script for network benchmark ping/bulk self-testing over loopback
	 device on single node.
   @test Script for tool ping/bulk testing with two test nodes.

   <hr>
   @section net-test-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   @subsection net-test-config-O Configuration

   - c2_net_test_config_init() have O(N*M) complexity, where N is number of
   command line parameters and M is number of all possible command line
   parameter names.
   - c2_net_test_config_fini() have O(1) complexity.
   - Every reading of configuration parameter have O(1) complexity.

   @subsection net-test-stats-O Statistics

   All c2_net_test_stats_* functions have O(1) complexity.

   <hr>
   @section net-test-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   @anchor net-test-hld
   - <a href="https://docs.google.com/a/xyratex.com/document/view?id=1_dAYA4_5rLEr9Z4PSkH1OTDTMlGBmAcIEEJRvwCRDV4">Colibri Network Benchmark HLD</a>
   - <a href="http://wiki.lustre.org/manual/LustreManual20_HTML/LNETSelfTest.html">LNET Self-Test manual</a>
   - <a href="http://reviewboard.clusterstor.com/r/773">DLD review request</a>

 */

#include "lib/cdefs.h"
#include "lib/thread.h"

#include "net/test/node_config.h"
#include "net/test/node_main.h"

/**
   @defgroup NetTestInternals Colibri Network Benchmark Internals

   @see @ref net-test

   @{
 */

static struct c2_thread net_test_main_thread;

/**
   Entry point for test node.
 */
static void net_test_main(int ignored)
{
	/* TODO */
}

int c2_net_test_init(struct c2_net_test_node_config *cfg)
{
	return C2_THREAD_INIT(&net_test_main_thread, int, NULL,
		            &net_test_main, 0, "net_test_main");
}
C2_EXPORTED(c2_net_test_init);

void c2_net_test_fini(void)
{
	/* TODO */
	c2_thread_join(&net_test_main_thread);
}
C2_EXPORTED(c2_net_test_fini);

/**
   @} end NetTestInternals
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
