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

#ifdef __KERNEL__
#include <linux/kernel.h>
#endif

/**
   @defgroup NetTestConfigDFS Colibri Network Benchmark \
			      Configuration \
			      Detailed Functional Specification

   @see @ref net-test

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
extern long c2_net_test_config_count;

/**
   Size of bulk test message.
   @see @ref net-test-config-fspec-cli
 */
extern long c2_net_test_config_size;

/**
   Test server names for test client.
   @see @ref net-test-config-fspec-cli
 */
extern char **c2_net_test_config_targets;

/**
   Test server names number for test client.
   @see @ref net-test-config-fspec-cli
 */
extern long c2_net_test_config_targets_nr;

/**
   Test console name.
   @see @ref net-test-config-fspec-cli
 */
extern char *c2_net_test_config_console;

/** Default parameter values */
enum {
	C2_NET_TEST_CONFIG_COUNT_DEFAULT = 16,
	C2_NET_TEST_CONFIG_SIZE_DEFAULT	 = (1024 * 1024),
	C2_NET_TEST_CONFIG_TARGETS_MAX	 = 0x100,
};

#define C2_NET_TEST_CONFIG_CONSOLE_DEFAULT    NULL

/**
   Set configuration variable values according to command line parameters.
 */
int c2_net_test_config_init(void);

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
#include "net/test/linux_kernel/node_config_k.h"
#else
#include "net/test/user_space/node_config.h"
#endif

/**
   @} end of NetTestConfigDFS
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
