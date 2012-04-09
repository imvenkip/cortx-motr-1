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

#ifndef __NET_TEST_CLIENT_H__
#define __NET_TEST_CLIENT_H__

/**
   @page net-test-client-fspec Functional Specification

   - @ref net-test-client-fspec-ds
   - @ref net-test-client-fspec-sub
   - @ref net-test-client-fspec-cli
   - @ref net-test-client-fspec-usecases
   - @subpage NetTestClientDFS "Detailed Functional Specification"
   - @subpage NetTestClientInternals "Internals"

   @section net-test-client-fspec-ds Data Structures

   @section net-test-client-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   @subsection net-test-client-fspec-sub-cons Constructors and Destructors

   - c2_net_test_client_init()
   - c2_net_test_client_fini()

   @subsection net-test-client-fspec-sub-acc Accessors and Invariants

   @subsection net-test-client-fspec-sub-opi Operational Interfaces

   - c2_net_test_client_start()
   - c2_net_test_client_stop()

   @section net-test-client-fspec-cli Command Usage
   <i>Mandatory for command line programs.  Components that provide programs
   would provide a specification of the command line invocation arguments.  In
   addition, the format of any any structured file consumed or produced by the
   interface must be described in this section.</i>

   @section net-test-client-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>

   Test client can be run in such way:

   @code
int rc;
rc = c2_net_test_client_init();
if (rc == 0) {
	rc = c2_net_test_client_start();
	if (rc == 0)
	c2_net_test_client_stop();
}
c2_net_test_client_fini();
return rc;
   @endcode

   @see
   @ref net-test-client @n
   @ref NetTestClientDFS "Detailed Functional Specification" @n
   @ref NetTestClientInternals "Internals" @n
 */

/**
   @defgroup NetTestClientDFS Colibri Network Benchmark Test Client

   @see
   @ref net-test-client

   @{
*/

/**
   Initialize client data structures.
   c2_net_test_network_init() and c2_net_test_config_init() must be called
   before this function.
   @see @ref net-test-client-lspec
 */
int c2_net_test_client_init(void);

/**
   Finalize client data structures.
   @see @ref net-test-client-lspec
 */
void c2_net_test_client_fini(void);

/**
   Start test client.
   This function will return only after test client finished or interrupted
   with c2_net_test_client_stop().
   @see @ref net-test-client-lspec
 */
int c2_net_test_client_start(void);

/**
   Stop test client.
   @see @ref net-test-client-lspec
 */
void c2_net_test_client_stop(void);

/**
   @} end NetTestClientDFS
*/

#endif /*  __NET_TEST_CLIENT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */

