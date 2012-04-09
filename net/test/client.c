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

/**
   @page net-test-client Test Client

   - @ref net-test-client-ovw
   - @ref net-test-client-def
   - @subpage net-test-client-fspec "Functional Specification"
   - @ref net-test-client-lspec
   - @ref net-test-client-lspec-thread
   - @ref net-test-client-lspec-numa
   - @ref net-test-client-ut
   - @ref net-test-client-O
   - @ref net-test-client-ref

   <hr>
   @section net-test-client-ovw Overview

   This document is intended to describe test client.
   @note This document will be extended on CODE phase.

   <hr>
   @section net-test-client-def Definitions
   @see @ref net-test-def

   <hr>
   @section net-test-client-lspec Logical Specification

   For every test server will be one thread, which will send and receive
   test messages.

   - @ref net-test-client-lspec-ping
   - @ref net-test-client-lspec-bulk
   - @ref net-test-client-lspec-thread
   - @ref net-test-client-lspec-numa

   @subsection net-test-client-lspec-ping Ping Test

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

   @subsection net-test-client-lspec-bulk Bulk Test

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


   @subsection net-test-client-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   @subsection net-test-client-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   <hr>
   @section net-test-client-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>
   @todo add

   <hr>
   @section net-test-client-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   <hr>
   @section net-test-client-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   @ref net-test

 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "net/test/client.h"

/**
   @defgroup NetTestClientInternals Colibri Network Bencmark Test Client Internals

   @see
   @ref net-test-client

   @{
 */

int c2_net_test_client_init(void)
{
}

void c2_net_test_client_fini(void)
{
}

int c2_net_test_client_start(void)
{
}

void c2_net_test_client_stop(void)
{
}

/**
   @} end NetTestClientInternals
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

