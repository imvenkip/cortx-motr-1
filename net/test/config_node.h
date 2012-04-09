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

#ifndef __NET_TEST_CONFIG_NODE_H__
#define __NET_TEST_CONFIG_NODE_H__

/**
   @page net-test-config-fspec Functional Specification

   - @ref net-test-config-fspec-ds
   - @ref net-test-config-fspec-sub
   - @ref net-test-config-fspec-cli
   - @ref net-test-config-fspec-usecases
   - @subpage NetTestStatsDFS "Detailed Functional Specification"
   - @subpage NetTestStatsInternals "Internals"

   @section net-test-config-fspec-ds Data Structures

   @section net-test-config-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   @subsection net-test-config-fspec-sub-cons Constructors and Destructors

   - c2_net_test_config_init()
   - c2_net_test_config_fini()

   @subsection net-test-config-fspec-sub-acc Accessors and Invariants

   - c2_net_test_config_invariant()

   @subsection net-test-config-fspec-sub-opi Operational Interfaces

   @section net-test-config-fspec-cli Command Usage
   <i>Mandatory for command line programs.  Components that provide programs
   would provide a specification of the command line invocation arguments.  In
   addition, the format of any any structured file consumed or produced by the
   interface must be described in this section.</i>

   This command line options is valid for both client and server testing role.

   - <b>role</b> Program role. Mandatory option.
     - <b>client</b> Program will act as test client.
     - <b>server</b> Program will act as test server.
   - <b>type</b> Test type. Mandatory option.
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

   @section net-test-config-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>

   role=client type=ping count=10 target=s1,s2,s3 @n
   Run ping test as test client with 10 test messages to servers s1, s2 and s3.
   @n @n
   role=server type=bulk target=c1,c2 @n
   Run bulk test as test server with 1Mb bulk message size and
   test clients c1 and c2.

   @see
   @ref net-test-config @n
   @ref NetTestConfigDFS "Detailed Functional Specification" @n
   @ref NetTestConfigInternals "Internals" @n
 */

/**
   @defgroup NetTestConfigDFS Colibri Network Benchmark Configuration

   @see @ref net-test-config

   @{
*/

/**
   Test node role - node can be test client or test server.
 */
enum c2_net_test_role {
	C2_NET_TEST_ROLE_CLIENT,
	C2_NET_TEST_ROLE_SERVER
};

/**
   Test type - can be ping test or bulk test.
 */
enum c2_net_test_type {
	C2_NET_TEST_TYPE_PING,
	C2_NET_TEST_TYPE_BULK
};

/**
   Test node role.
   @see @ref net-test-config-fspec-cli
 */
extern enum c2_net_test_role c2_net_test_config_role;

/**
   Test node type.
   @see @ref net-test-config-fspec-cli
 */
extern enum c2_net_test_type c2_net_test_config_type;

/**
   Number of packets which need to be send to test server
   @see @ref net-test-config-fspec-cli
 */
extern long		     c2_net_test_config_count;
/** Default value for c2_net_test_config_count */
const long		     c2_net_test_config_count_default = 16;

/**
   Size of bulk test message.
   @see @ref net-test-config-fspec-cli
 */
extern long		     c2_net_test_config_size;
/** Default value for c2_net_test_config_size */
const long		     c2_net_test_config_size_default = 1024 * 1024;

/**
   Test server names for test client.
   @see @ref net-test-config-fspec-cli
 */
extern char		   **c2_net_test_config_targets;
/** Default value for c2_net_test_config_targets */
const char		   **c2_net_test_config_targets_default = NULL;

/**
   Test server names number for test client.
   @see @ref net-test-config-fspec-cli
 */
extern long		     c2_net_test_config_targets_nr;
/** Default value for c2_net_test_config_targets_nr */
const long		     c2_net_test_config_targets_nr_default = 0;

/**
   Test console name.
   @see @ref net-test-config-fspec-cli
 */
extern char		    *c2_net_test_config_console;
/** Default value for c2_net_test_config_console */
const char		    *c2_net_test_config_console_default = NULL;


/**
   Set configuration variable values according to command line parameters.
 */
int  c2_net_test_config_init(void);

/**
   Finalize configuration module (free memory etc.)
 */
void c2_net_test_config_fini(void);

/**
   Do sanity check for configuration variables.
   @return configuration is valid.
 */
bool c2_net_test_config_invariant(void);

#ifdef __KERNEL__
#include "net/test/linux_kernel/config.h"
#else
#include "net/test/user_space/config.h"
#endif

/**
   @} NetTestConfigDFS end group
 */

#endif /*  __NET_TEST_CONFIG_NODE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
