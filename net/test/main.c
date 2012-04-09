/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
      - @subpage net-test-stats
      - @subpage net-test-config
      - @subpage net-test-network
      - @subpage net-test-client
      - @subpage net-test-server
      - @subpage net-test-console
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

   @note Network operations description will be added on CODE phase because
   I need to write some working code before writing DLD.
   @note UT description will be added on CODE and UT phase.

   <hr>
   @section net-test-def Definitions
   <i>Mandatory.
   The DLD shall provide definitions of the terms and concepts
   introduced by the design, as well as the relevant terms used by the
   specification but described elsewhere.  References to the
   C2 Glossary are permitted and encouraged.  Agreed upon terminology
   should be incorporated in the glossary.</i>

   Previously defined terms: see @ref net-test-hld "Colibri Network Benchmark HLD"

   New terms:

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
   @section net-test-lspec Logical Specification
   <i>Mandatory.  This section describes the internal design of the component,
   explaining how the functional specification is met.  Sub-components and
   diagrams of their interaction should go into this section.  The section has
   mandatory subsections created using the Doxygen @@subsection command.  The
   designer should feel free to use additional sub-sectioning if needed, though
   if there is significant additional sub-sectioning, provide a table of
   contents here.</i>

   - @ref net-test-lspec-comps
   - @ref net-test-stats
   - @ref net-test-config
   - @ref net-test-network
   - @ref net-test-client
   - @ref net-test-server
   - @ref net-test-console
   - @ref net-test-lspec-state
   - @ref net-test-lspec-thread
   - @ref net-test-lspec-numa

   @subsection net-test-lspec-comps Component Overview
   <i>Mandatory.
   This section describes the internal logical decomposition.
   A diagram of the interaction between internal components and
   between external consumers and the internal components is useful.</i>
   @dot
   digraph {
     node [style=box];
     label = "Network Benchmark Components and Interactions";
     subgraph programs {
       label = "Programs";
       client [label = "Test client"];
       server [label = "Test server"];
       console [label = "Test console"];
     }
     subgraph libraries {
       label = "Libraries";
       config [label="Configuration"];
       stats [label="Statistics collector"];
       network [label="Network"];
     }
     client  -> config;
     client  -> stats;
     client  -> network;
     server  -> config;
     server  -> stats;
     server  -> network;
     console -> config;
     console -> stats;
     console -> network;
   }
   @enddot

   @subsection net-test-lspec-state State Specification
   <i>Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.</i>

   @subsection net-test-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   @subsection net-test-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   <hr>
   @section net-test-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref net-test-req section,
   and explains briefly how the DLD meets the requirement.</i>

   <hr>
   @section net-test-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>

   @todo add

   <hr>
   @section net-test-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   @todo add

   <hr>
   @section net-test-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   <hr>
   @section net-test-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   @anchor net-test-hld
   - <a href="https://docs.google.com/a/xyratex.com/document/view?id=1_dAYA4_5rLEr9Z4PSkH1OTDTMlGBmAcIEEJRvwCRDV4">Colibri Network Benchmark HLD</a>
   - <a href="http://wiki.lustre.org/manual/LustreManual20_HTML/LNETSelfTest.html">LNET Self-Test manual</a>

 */

#include "net/test/main.h"

/**
   @defgroup NetTestInternals Colibri Network Benchmark Internals

   @see @ref net-test

   @{
 */

/**
   Entry point for test node.
 */
void c2_net_test_main(void);

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
