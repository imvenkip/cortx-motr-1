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
   @page net-test-server Test Server

   - @ref net-test-server-ovw
   - @ref net-test-server-def
   - @subpage net-test-server-fspec "Functional Specification"
   - @ref net-test-server-lspec
   - @ref net-test-server-lspec-thread
   - @ref net-test-server-lspec-numa
   - @ref net-test-server-ut
   - @ref net-test-server-O
   - @ref net-test-server-ref

   <hr>
   @section net-test-server-ovw Overview

   This document is intended to describe test server.
   @note This document will be extended on CODE phase.

   <hr>
   @section net-test-server-def Definitions
   @see @ref net-test-def

   <hr>
   @section net-test-server-lspec Logical Specification
   @see @ref net-test-client-lspec "Test Client Logical Specification"

   @subsection net-test-server-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   @subsection net-test-server-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   <hr>
   @section net-test-server-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>
   @todo add

   <hr>
   @section net-test-server-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   <hr>
   @section net-test-server-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   @ref net-test

 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "net/test/server.h"

/**
   @defgroup NetTestServerInternals Colibri Network Bencmark Test Server Internals

   @see
   @ref net-test-server

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
   @} end NetTestServerInternals
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


