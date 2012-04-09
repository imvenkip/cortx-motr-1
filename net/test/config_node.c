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
   @page net-test-config Configuration for Test Node

   - @ref net-test-config-ovw
   - @ref net-test-config-def
   - @subpage net-test-config-fspec "Functional Specification"
   - @ref net-test-config-lspec
   - @ref net-test-config-lspec-thread
   - @ref net-test-config-lspec-numa
   - @ref net-test-config-ut
   - @ref net-test-config-O
   - @ref net-test-config-ref

   <hr>
   @section net-test-config-ovw Overview

   This document is intended to describe test node configuration for
   @ref net-test.

   <hr>
   @section net-test-config-def Definitions

   - <b>Command line parameters</b> Command line parameters for user space
   program or kernel module options for kernel module.
   - <b>Configuration variable</b> Variable with name. It can have some value.
   - <b>Configuration</b> Set of name-value pairs.

   <hr>
   @section net-test-config-lspec Logical Specification

   - Typed variables are used to store configuration.
   - Configuration variables are set in c2_net_test_config_init(). They
   should be never changed in other place.

   @subsection net-test-config-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   Configuration is not protected by any synchronization mechanism.
   Configuration is not intented to change after initial initialization,
   so no need to use synchronization mechamish for reading configuration.

   @subsection net-test-config-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   Configuration is not intented to change after initial initialization,
   so cache coherence overhead will not exists.

   <hr>
   @section net-test-config-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>
   @todo add

   <hr>
   @section net-test-config-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   - c2_net_test_config_init() have O(N*M) complexity, where N is number of
   command line parameters and M is number of all possible command line
   parameter names.
   - c2_net_test_config_fini() have O(1) complexity.
   - Every reading of parameter have O(1) complexity.

   <hr>
   @section net-test-config-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   @ref net-test

 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "net/test/config_node.h"

/**
   @defgroup NetTestConfigInternals Colibri Network Bencmark Configuration Internals

   @see
   @ref net-test-config

   @{
 */

enum c2_net_test_role c2_net_test_config_role;
enum c2_net_test_type c2_net_test_config_type;
long		      c2_net_test_config_count;
long		      c2_net_test_config_size;
char		    **c2_net_test_config_targets;
long		      c2_net_test_config_targets_nr;
char		     *c2_net_test_config_console;

int  c2_net_test_config_init(void)
{
}

void c2_net_test_config_fini(void)
{
}

bool c2_net_test_config_invariant(void)
{
}

/**
   @} end NetTestConfigInternals
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
