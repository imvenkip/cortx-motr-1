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

#ifndef __NET_TEST_SERVICE_H__
#define __NET_TEST_SERVICE_H__

#include "lib/time.h"			/* c2_time_t */
#include "lib/thread.h"			/* c2_thread */

#include "net/test/commands.h"		/* c2_net_test_cmd_ctx */
#include "net/test/node.h"		/* c2_net_test_node_ctx */

/**
   @defgroup NetTestServiceDFS Test Service
   @ingroup NetTestDFS

   Test services:
   - ping test client/server;
   - bulk test client/server;

   @see
   @ref net-test

   @{
 */

struct c2_net_test_service;

/** Service command handler */
struct c2_net_test_service_cmd_handler {
	/** Command type to handle */
	enum c2_net_test_cmd_type ntsch_type;
	/** Handler */
	int (*ntsch_handler)(void *ctx,
			     const struct c2_net_test_cmd *cmd,
			     struct c2_net_test_cmd *reply);
};

/** Service state */
enum c2_net_test_service_state {
	/** Service is not initialized */
	C2_NET_TEST_SERVICE_UNINITIALIZED = 0,
	/** Service is ready to handle commands */
	C2_NET_TEST_SERVICE_READY,
	/** Service was finished. Can be set by service operations */
	C2_NET_TEST_SERVICE_FINISHED,
	/** Service was failed. Can be set by service operations */
	C2_NET_TEST_SERVICE_FAILED,
	/** Number of service states */
	C2_NET_TEST_SERVICE_NR
};

/** Service operations */
struct c2_net_test_service_ops {
	/** Service initializer. Returns NULL on failure. */
	void *(*ntso_init)(struct c2_net_test_service *svc);
	/** Service finalizer. */
	void (*ntso_fini)(void *ctx);
	/** Take on step. Executed if no commands received. */
	int  (*ntso_step)(void *ctx);
	/** Command handlers. */
	struct c2_net_test_service_cmd_handler *ntso_cmd_handler;
	/** Number of command handlers. */
	size_t					ntso_cmd_handler_nr;
};

/** Service state machine */
struct c2_net_test_service {
	/** Test service context. It will be passed to the service ops */
	void			       *nts_svc_ctx;
	/** Service operations */
	struct c2_net_test_service_ops *nts_ops;
	/** Service state */
	enum c2_net_test_service_state  nts_state;
	/** errno from last service operation */
	int			        nts_errno;
};

/**
   Initialize test service.
   Typical pattern to use test service:

   @code
   c2_net_test_service_init();
   while (state != FAILED && state != FINISHED) {
	   command_was_received = try_to_receive_command();
	   if (command_was_received)
		c2_net_test_service_cmd_handle(received_command);
	   else
		c2_net_test_service_step();
   }
   c2_net_test_service_fini();
   @endcode

   @note Service state will not be changed it ops->ntso_init returns
   non-zero result and will be changed to C2_NET_TEST_SERVICE_READY
   otherwise.

   @param svc Test service
   @param ops Service operations
   @post ergo(result == 0, c2_net_test_service_invariant(svc))
 */
int c2_net_test_service_init(struct c2_net_test_service *svc,
			     struct c2_net_test_service_ops *ops);

/**
   Finalize test service.
   Service can be finalized from any state except
   C2_NET_TEST_SERVICE_UNINITIALIZED.
   @pre c2_net_test_service_invariant(svc)
   @pre (svc->nts_state != C2_NET_TEST_SERVICE_UNINITIALIZED)
   @post c2_net_test_service_invariant(svc)
 */
void c2_net_test_service_fini(struct c2_net_test_service *svc);

/** Test service invariant. */
bool c2_net_test_service_invariant(struct c2_net_test_service *svc);

/**
   Take one step. It can be only done from C2_NET_TEST_SERVICE_READY state.
   @pre c2_net_test_service_invariant(svc)
   @post c2_net_test_service_invariant(svc)
 */
int c2_net_test_service_step(struct c2_net_test_service *svc);

/**
   Handle command and fill reply
   @pre c2_net_test_service_invariant(svc)
   @post c2_net_test_service_invariant(svc)
 */
int c2_net_test_service_cmd_handle(struct c2_net_test_service *svc,
				   struct c2_net_test_cmd *cmd,
				   struct c2_net_test_cmd *reply);

/**
   Change service state. Can be called from service ops.
   @pre c2_net_test_service_invariant(svc)
   @post c2_net_test_service_invariant(svc)
 */
void c2_net_test_service_state_change(struct c2_net_test_service *svc,
				      enum c2_net_test_service_state state);

/**
   Get service state.
   @pre c2_net_test_service_invariant(svc)
 */
enum c2_net_test_service_state
c2_net_test_service_state_get(struct c2_net_test_service *svc);

/**
   @} end of NetTestServiceDFS group
 */

#endif /*  __NET_TEST_SERVICE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
