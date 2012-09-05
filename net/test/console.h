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
 * Original creation date: 09/03/2012
 */

#pragma once

#ifndef __NET_TEST_CONSOLE_H__
#define __NET_TEST_CONSOLE_H__

#include "lib/time.h"			/* c2_time_t */
#include "lib/types.h"			/* c2_bcount_t */

#include "net/test/commands.h"		/* c2_net_test_cmd_ctx */
#include "net/test/node.h"		/* c2_net_test_role */
#include "net/test/slist.h"		/* c2_net_test_slist */


/**
   @defgroup NetTestConsoleDFS Test Console
   @ingroup NetTestDFS

   @see
   @ref net-test

   @{
 */

/**
 * Console configuration.
 * Set by console user before calling c2_net_test_init()
 */
struct c2_net_test_console_cfg {
	/** Console commands endpoint address for the test servers */
	char			*ntcc_addr_console4servers;
	/** Console commands endpoint address for the test clients */
	char			*ntcc_addr_console4clients;
	/** List of server command endpoints */
	struct c2_net_test_slist ntcc_servers;
	/** List of client command endpoints */
	struct c2_net_test_slist ntcc_clients;
	/** List of server command endpoints */
	struct c2_net_test_slist ntcc_data_servers;
	/** List of client command endpoints */
	struct c2_net_test_slist ntcc_data_clients;
	/** Commands send timeout for the test nodes */
	c2_time_t		 ntcc_cmd_send_timeout;
	/** Commands receive timeout for the test nodes */
	c2_time_t		 ntcc_cmd_recv_timeout;
	/** Test messages send timeout for the test nodes */
	c2_time_t		 ntcc_buf_send_timeout;
	/** Test messages receive timeout for the test nodes */
	c2_time_t		 ntcc_buf_recv_timeout;
	/** Test type */
	enum c2_net_test_type	 ntcc_test_type;
	/** Number of test messages for the test client */
	size_t			 ntcc_msg_nr;
	/** Test messages size */
	c2_bcount_t		 ntcc_msg_size;
	/**
	   Test server concurrency.
	   @see c2_net_test_cmd_init.ntci_concurrency
	 */
	size_t			 ntcc_concurrency_server;
	/**
	   Test client concurrency.
	   @see c2_net_test_cmd_init.ntci_concurrency
	 */
	size_t			 ntcc_concurrency_client;
};

/** Test console context for the node role */
struct c2_net_test_console_role_ctx {
	/** Commands structure */
	struct c2_net_test_cmd_ctx	   *ntcrc_cmd;
	/** Accumulated status data */
	struct c2_net_test_cmd_status_data *ntcrc_sd;
	/** Number of nodes */
	size_t				    ntcrc_nr;
	/** -errno for the last function */
	int				   *ntcrc_errno;
	/** status of last received *_DONE command */
	int				   *ntcrc_status;
};

/** Test console context */
struct c2_net_test_console_ctx {
	/** Test console configuration */
	struct c2_net_test_console_cfg	   *ntcc_cfg;
	/** Test clients */
	struct c2_net_test_console_role_ctx ntcc_clients;
	/** Test servers */
	struct c2_net_test_console_role_ctx ntcc_servers;
};

int c2_net_test_console_init(struct c2_net_test_console_ctx *ctx,
			     struct c2_net_test_console_cfg *cfg);

void c2_net_test_console_fini(struct c2_net_test_console_ctx *ctx);

/**
   Send command from console to the set of test nodes and wait for reply.
   @param ctx Test console context.
   @param role Test node role. Test console will send commands to
	       nodes with this role only.
   @param cmd_type Command type. Test console will create and send command
		   with this type and wait for the corresponding reply
		   type (for example, if cmd_type == C2_NET_TEST_CMD_INIT,
		   then test console will wait for C2_NET_TEST_CMD_INIT_DONE
		   reply).
   @return number of successfully received replies for sent command.
 */
size_t c2_net_test_console_cmd(struct c2_net_test_console_ctx *ctx,
			       enum c2_net_test_role role,
			       enum c2_net_test_cmd_type cmd_type);

/**
   @} end of NetTestConsoleDFS group
 */

#endif /*  __NET_TEST_CONSOLE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
