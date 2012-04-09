/* -*- C -*- */
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
   - @ref NetTestDFS "Detailed Functional Specification" <!-- Note link -->

   @section net-test-fspec-ds Data Structures
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and <i>brief</i> description of the
   major externally visible data structures defined by this component.  No
   details of the data structure are required here, just the salient
   points.</i>

   @section net-test-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   @subsection net-test-fspec-sub-cons Constructors and Destructors

   - c2_net_test_main()

   @subsection net-test-fspec-sub-acc Accessors and Invariants

   @subsection net-test-fspec-sub-opi Operational Interfaces

   @section net-test-fspec-cli Command Usage
   <i>Mandatory for command line programs.  Components that provide programs
   would provide a specification of the command line invocation arguments.  In
   addition, the format of any any structured file consumed or produced by the
   interface must be described in this section.</i>

   @see @ref net-test-config-fspec-cli

   @section net-test-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>

   @see @ref net-test
 */

/**
   @defgroup NetTestDFS Colibri Network Benchmark.
   @brief Detailed functional specification for Colibri Network Benchmark.

   @see @ref net-test

   @{
*/

/**
   Main function for client/server.
   It will determine whether it is a client or a server and
   will run corresponding subroutine.
*/
void c2_net_test_main(void);

/**
   @} NetTestDFS end group
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
