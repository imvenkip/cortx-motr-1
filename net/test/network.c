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
   @page net-test-network Network

   - @ref net-test-network-ovw
   - @ref net-test-network-def
   - @subpage net-test-network-fspec "Functional Specification"
   - @ref net-test-network-lspec
   - @ref net-test-network-lspec-thread
   - @ref net-test-network-lspec-numa
   - @ref net-test-network-ut
   - @ref net-test-network-O
   - @ref net-test-network-ref

   <hr>
   @section net-test-network-ovw Overview

   This document is intended to describe network subsystem of @ref net-test.
   @note I will fill it on CODE phase.

   <hr>
   @section net-test-network-def Definitions

   <hr>
   @section net-test-network-lspec Logical Specification

   @subsection net-test-network-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   @subsection net-test-network-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   <hr>
   @section net-test-network-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>
   @todo add

   <hr>
   @section net-test-network-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   <hr>
   @section net-test-network-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   @ref net-test

 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "net/test/network.h"

/**
   @defgroup NetTestNetworkInternals Colibri Network Bencmark Network Internals

   @see
   @ref net-test-network

   @{
 */

/**
   @} end NetTestNetworkInternals
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
