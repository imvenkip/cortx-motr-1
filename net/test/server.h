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
 * Original author Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 03/22/2012
 */

#ifndef __NET_TEST_SERVER_H__
#define __NET_TEST_SERVER_H__

/**
   @page net-test-server-fspec Functional Specification

   - @ref net-test-server-fspec-ds
   - @ref net-test-server-fspec-sub
   - @ref net-test-server-fspec-cli
   - @ref net-test-server-fspec-usecases
   - @subpage NetTestServerDFS "Detailed Functional Specification"
   - @subpage NetTestServerInternals "Internals"

   @section net-test-server-fspec-ds Data Structures

   @section net-test-server-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   @subsection net-test-server-fspec-sub-cons Constructors and Destructors

   - c2_net_test_server_init()
   - c2_net_test_server_fini()

   @subsection net-test-server-fspec-sub-acc Accessors and Invariants

   @subsection net-test-server-fspec-sub-opi Operational Interfaces

   - c2_net_test_server_start()
   - c2_net_test_server_stop()

   @section net-test-server-fspec-cli Command Usage
   <i>Mandatory for command line programs.  Components that provide programs
   would provide a specification of the command line invocation arguments.  In
   addition, the format of any any structured file consumed or produced by the
   interface must be described in this section.</i>

   @section net-test-server-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>
   @todo add

   @see
   @ref net-test-server @n
   @ref NetTestServerDFS "Detailed Functional Specification" @n
   @ref NetTestServerInternals "Internals" @n
 */

/**
   @defgroup NetTestServerDFS Colibri Network Benchmark Test Server

   @see
   @ref net-test-server

   @{
*/

/**
   Initialize client data structures.
   c2_net_test_network_init() and c2_net_test_config_init() must be called
   before this function.
   @see @ref net-test-server-lspec
 */
int c2_net_test_server_init(void);

/**
   Finalize client data structures.
   @see @ref net-test-server-lspec
 */
void c2_net_test_server_fini(void);

/**
   Start test client.
   This function will return only after test client finished or interrupted
   with c2_net_test_server_stop().
   @see @ref net-test-server-lspec
 */
int c2_net_test_server_start(void);

/**
   Stop test client.
   @see @ref net-test-server-lspec
 */
void c2_net_test_server_stop(void);


/**
   @} end NetTestServerDFS
*/

#endif /*  __NET_TEST_SERVER_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */


