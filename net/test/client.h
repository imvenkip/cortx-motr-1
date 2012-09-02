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
   @defgroup NetTestStatsBandwidthDFS Bandwidth Statistics
   @ingroup NetTestDFS

   @see
   @ref net-test

   @{
 */

/** Bandwidth statistics, measured in bytes/sec */
struct c2_net_test_stats_bandwidth {
	/** Statistics */
	struct c2_net_test_stats ntsb_stats;
	/** Last check number of bytes */
	c2_bcount_t		 ntsb_bytes_last;
	/** Last check time */
	c2_time_t		 ntsb_time_last;
	/** Time interval to check */
	c2_time_t		 ntsb_time_interval;
};

/**
   Initialize bandwidth statistics.
   @param sb Bandwidth statistics structure.
   @param bytes Next call to c2_net_test_stats_bandwidth_add() will use
		this value as previous value to measure number of bytes
		transferred in time interval.
   @param timestamp The same as bytes, but for time difference.
   @param interval Bandwidth measure interval.
		   c2_net_test_stats_bandwidth_add() will not add sample
		   to stats if interval from last addition to statistics
		   is less than interval.
 */
void c2_net_test_stats_bandwidth_init(struct c2_net_test_stats_bandwidth *sb,
				      c2_bcount_t bytes,
				      c2_time_t timestamp,
				      c2_time_t interval);

/**
   Add sample to the bandwidth statistics if time interval
   [sb->ntsb_time_last, timestamp] is greater than sb->ntsb_interval.
   This function will use previous call (or initializer) parameters to
   calculate bandwidth: number of bytes [sb->ntsb_bytes_last, bytes]
   in the time range [sb->ntsb_time_last, timestamp].
   @param sb Bandwidth statistics structure.
   @param bytes Total number of bytes transferred.
   @param timestamp Timestamp of bytes value.
   @return Value will not be added to the sample before this time.
 */
c2_time_t
c2_net_test_stats_bandwidth_add(struct c2_net_test_stats_bandwidth *sb,
				c2_bcount_t bytes,
				c2_time_t timestamp);

/**
   @} end of NetTestStatsBandwidthDFS group
 */

/**
   @defgroup NetTestPingNodeDFS Ping Node
   @ingroup NetTestDFS

   @todo split this file.

   @{
 */

extern struct c2_net_test_service_ops c2_net_test_node_ping_ops;

/**
   @} end of NetTestPingNodeDFS group
 */

/**
   @defgroup NetTestMsgNRDFS Messages Number
   @ingroup NetTestDFS

   @{
 */

/** Sent/received test messages number. */
struct c2_net_test_msg_nr {
	/** Number of sent test messages */
	struct c2_atomic64 ntmn_sent;
	/** Number of received test messages */
	struct c2_atomic64 ntmn_rcvd;
	/** Number of errors while receiving test messages */
	struct c2_atomic64 ntmn_send_failed;
	/** Number of errors while sending test messages */
	struct c2_atomic64 ntmn_recv_failed;
};

/**
   Reset all messages number statistics to 0.
 */
void c2_net_test_msg_nr_reset(struct c2_net_test_msg_nr *msg_nr);

/**
   Copy messages number statistics to c2_net_test_cmd_status_data.
   Algorithm:
   1. Copy statistics from sd to local variables, one by one field.
   2. Copy statistics from msg_nr to sd, one by one field.
   3. Compare values in sd and local variables - goto 1 if they aren't equal.
 */
void c2_net_test_msg_nr_get_lockfree(struct c2_net_test_msg_nr *msg_nr,
				     struct c2_net_test_cmd_status_data *sd);

/**
   @} end of NetTestMsgNRDFS group
 */

/**
   @defgroup NetTestClientDFS Test Client
   @ingroup NetTestDFS

   @see
   @ref net-test

   @{
 */

/** Node state */
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
	/** Node endpoint address (for commands) */
	char	 *ntnc_addr;
	/** Console endpoint address (for commands) */
	char	 *ntnc_addr_console;
	/** Send commands timeout. @see c2_net_test_commands_init(). */
	c2_time_t ntnc_send_timeout;
};

/** Node context. */
struct c2_net_test_node_ctx {
	/** Commands context. Connected to the test console. */
	struct c2_net_test_cmd_ctx     ntnc_cmd;
	/** Network context for testing */
	struct c2_net_test_network_ctx ntnc_net;
	/** Test service */
	struct c2_net_test_service    *ntnc_svc;
	/** Service private data. Set and used in service implementations. */
	void			      *ntnc_svc_private;
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
	 * 'node-thread-was-finished' semaphore.
	 * Initialized to 0. External routines can down() or timeddown()
	 * this semaphore to wait for the node thread.
	 * up() at the end of the node thread.
	 */
	struct c2_semaphore	       ntnc_thread_finished_sem;
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
   Get c2_net_test_node_ctx from c2_net_test_network_ctx.
   Useful in the network buffer callbacks.
 */
struct c2_net_test_node_ctx
*c2_net_test_node_ctx_from_net_ctx(struct c2_net_test_network_ctx *net_ctx);

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
	int (*ntsch_handler)(struct c2_net_test_node_ctx *ctx,
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
	/** Service initializer. */
	int  (*ntso_init)(struct c2_net_test_node_ctx *ctx);
	/** Service finalizer. */
	void (*ntso_fini)(struct c2_net_test_node_ctx *ctx);
	/** Take on step. Executed if no commands received. */
	int  (*ntso_step)(struct c2_net_test_node_ctx *ctx);
	/** Command handlers. */
	struct c2_net_test_service_cmd_handler *ntso_cmd_handler;
	/** Number of command handlers. */
	size_t					ntso_cmd_handler_nr;
};

/** Service state machine */
struct c2_net_test_service {
	/** Test node context. It will be passed to the service ops */
	struct c2_net_test_node_ctx    *nts_node_ctx;
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

   @param svc	   Test service
   @param node_ctx Node context
   @param ops	   Service operations
   @post ergo(result == 0, c2_net_test_service_invariant(svc))
 */
int c2_net_test_service_init(struct c2_net_test_service *svc,
			     struct c2_net_test_node_ctx *node_ctx,
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

