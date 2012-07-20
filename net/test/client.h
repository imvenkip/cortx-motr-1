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

#include "lib/time.h"			/* c2_time_t */
#include "lib/thread.h"			/* c2_thread */

#include "net/test/commands.h"		/* c2_net_test_cmd_ctx */
#include "net/test/node_config.h"	/* c2_net_test_role */

/**
   @defgroup NetTestClientDFS Test Client
   @ingroup NetTestDFS

   @see
   @ref net-test

   @{
 */

enum c2_net_test_node_state {
	C2_NET_TEST_NODE_UNINITIALIZED,
	C2_NET_TEST_NODE_INITIALIZED,
	C2_NET_TEST_NODE_RUNNING,
	C2_NET_TEST_NODE_FAILED,
	C2_NET_TEST_NODE_DONE,
	C2_NET_TEST_NODE_STOPPED,
};

/** Node configuration */
struct c2_net_test_node_cfg {
	/** Node endpoint address */
	char	 *ntnc_addr;
	/** Node endpoint address */
	char	 *ntnc_addr_console;
	/** Send commands timeout. @see c2_net_test_commands_init(). */
	c2_time_t ntnc_send_timeout;
};

/** Node context. */
struct c2_net_test_node_ctx {
	/** Commands context. Connected to the test console. */
	struct c2_net_test_cmd_ctx     ntnc_cmd;
	/** Node thread */
	struct c2_thread	       ntnc_thread;
	/**
	   Exit flag for the node thread.
	   Node thread will check this flag and will terminate if it is set.
	 */
	bool			       ntnc_exit_flag;
	/** Error code. Set in node thread if something goes wrong. */
	int			       ntnc_errno;
	/**
	   'command sent to console' semaphore
	   c2_semaphore_up() in commands 'command sent' callback
	   c2_semaphore_up() in c2_net_test_node_stop()
	   c2_semaphore_down() in the node thread
	 */
	struct c2_semaphore	       ntnc_sem_cmd_sent;
	/** Network context for testing */
	struct c2_net_test_network_ctx ntnc_net;
};

/**
   Initialize node data structures.
   @param ctx node context.
   @param cfg node configuration.
   @see @ref net-test-lspec
   @note ctx->ntnc_cfg should be set and should not be changed after
   c2_net_test_node_init().
 */
int c2_net_test_node_init(struct c2_net_test_node_ctx *ctx,
			  struct c2_net_test_node_cfg *cfg);

/**
   Finalize node data structures.
   @see @ref net-test-lspec
 */
void c2_net_test_node_fini(struct c2_net_test_node_ctx *ctx);

/**
   Invariant for c2_net_test_node_ctx.
 */
bool c2_net_test_node_invariant(struct c2_net_test_node_ctx *ctx);

/**
   Start test node.
   This function will return only after test node finished or interrupted
   with c2_net_test_node_stop().
   @see @ref net-test-lspec
 */
int c2_net_test_node_start(struct c2_net_test_node_ctx *ctx);

/**
   Stop test node.
   @see @ref net-test-lspec
 */
void c2_net_test_node_stop(struct c2_net_test_node_ctx *ctx);

/**
   @} end of NetTestClientDFS group
 */

/**
   @defgroup NetTestConsoleDFS Test Console
   @ingroup NetTestDFS

   @see
   @ref net-test

   @{
 */

struct c2_net_test_console_cfg {
	char			*ntcc_addr_console4servers;
	char			*ntcc_addr_console4clients;
	struct c2_net_test_slist ntcc_servers;
	struct c2_net_test_slist ntcc_clients;
};

struct c2_net_test_console_ctx {
	struct c2_net_test_cmd_ctx ntcc_cmd_server;
	struct c2_net_test_cmd_ctx ntcc_cmd_client;
};

int c2_net_test_console_init(struct c2_net_test_console_ctx *ctx,
			     struct c2_net_test_console_cfg *cfg);

void c2_net_test_console_fini(struct c2_net_test_console_ctx *ctx);

/**
   @} end of NetTestConsoleDFS group
 */

/**
   @defgroup NetTestServiceDFS Test Service
   @ingroup NetTestDFS

   Test services:
   - ping test server;
   - ping test client;
   - bulk test server;
   - bulk test client;
   - test console.

   @see
   @ref net-test

   @{
 */

struct c2_net_test_service;

struct c2_net_test_service_cmd_handler {
	/** Command type to handle */
	enum c2_net_test_cmd_type ntsch_type;
	/** Handler */
	int (*ntsch_handler)(struct c2_net_test_node_ctx *ctx,
			     const struct c2_net_test_cmd *cmd);
};

typedef int
(*c2_net_test_service_handler_t)(struct c2_net_test_node_ctx *ctx);

enum c2_net_test_service_state {
	C2_NET_TEST_SERVICE_UNINITIALIZED,
	C2_NET_TEST_SERVICE_READY,
	C2_NET_TEST_SERVICE_FINISHED,
	C2_NET_TEST_SERVICE_FAILED,
};

struct c2_net_test_service_ops {
	c2_net_test_service_handler_t		ntso_init;
	c2_net_test_service_handler_t		ntso_fini;
	c2_net_test_service_handler_t		ntso_step;
	struct c2_net_test_service_cmd_handler *ntso_cmd_handler;
	size_t					ntso_cmd_handler_nr;
};

/*
   init();
   try_recv_cmd();
   while (state != FAILED && state != FINISHED) {
	   if (received)
		handle_cmd
	   else
		step();
   }
   fini;
 */
/** Service state machine. @todo document it */
struct c2_net_test_service {
	struct c2_net_test_node_ctx    *nts_node_ctx;
	struct c2_net_test_service_ops *nts_ops;
	enum c2_net_test_service_state  nts_state;
	int			        nts_errno;
};

int c2_net_test_service_init(struct c2_net_test_service *svc,
			     struct c2_net_test_node_ctx *node_ctx,
			     struct c2_net_test_service_ops *ops);
void c2_net_test_service_fini(struct c2_net_test_service *svc);
bool c2_net_test_service_invariant(struct c2_net_test_service *svc);

int c2_net_test_service_step(struct c2_net_test_service *svc);
int c2_net_test_service_cmd_handle(struct c2_net_test_service *svc,
				   struct c2_net_test_cmd *cmd);

void c2_net_test_service_state_change(struct c2_net_test_service *svc,
				      enum c2_net_test_service_state state);

enum c2_net_test_service_state
c2_net_test_service_state_get(struct c2_net_test_service *svc);

/**
   @} end of NetTestServiceDFS group
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

