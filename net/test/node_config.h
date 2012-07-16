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

#include "lib/types.h"		/* bool */

/**
   @defgroup NetTestNodeConfigDFS Node Configuration
   @ingroup NetTestDFS

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
   Configuration structure for test node.
   @see @ref net-test-fspec-cli
 */
struct c2_net_test_node_config {
	/** Test node role */
	enum c2_net_test_role role;
	/** Test node type */
	enum c2_net_test_type type;
	/** Number of test messages which need to be send to test server */
	uint32_t count;
	/** Size of bulk test message */
	c2_bcount_t size;
	/** Test server names for the test client */
	char **targets;
	/** Test server names number for the test client */
	uint32_t targets_nr;
	/** Test console adress */
	char *console;
};

/** Default parameter values */
enum {
	C2_NET_TEST_CONFIG_COUNT_DEFAULT = 16,
	C2_NET_TEST_CONFIG_SIZE_DEFAULT	 = (1024 * 1024),
	C2_NET_TEST_CONFIG_TARGETS_MAX	 = 0x100,
};

#define C2_NET_TEST_CONFIG_CONSOLE_DEFAULT	NULL

/**
   Do sanity check for configuration variables.
   @return configuration is valid.
 */
bool c2_net_test_node_config_invariant(
		const struct c2_net_test_node_config *cfg);

/**
   @} end of NetTestNodeConfigDFS group
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
