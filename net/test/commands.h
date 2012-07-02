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
 * Original creation date: 05/05/2012
 */

#ifndef __NET_TEST_COMMANDS_H__
#define __NET_TEST_COMMANDS_H__

#include "lib/errno.h"			/* E2BIG */
#include "lib/semaphore.h"		/* c2_semaphore */

#include "net/test/slist.h"		/* c2_net_test_slist */
#include "net/test/node_config.h"	/* c2_net_test_role */
#include "net/test/network.h"		/* c2_net_test_network_ctx */

/**
   @defgroup NetTestCommandsDFS Colibri Network Benchmark Commands \
			      Detailed Functional Specification

   @see
   @ref net-test

   @{
 */

enum {
	/** @todo 16k, change */
	/** @todo send size, ack size, send command */
	C2_NET_TEST_CMD_SIZE_MAX     = 16384,
	/**
	   c2_net_test_cmd.ntc_buf_status will be set to this value
	   if buffer wasn't received within timeout from some endpoint
	   from endpoints list.
	 */
	C2_NET_TEST_CMD_NOT_RECEIVED = -E2BIG,
};

/**
   Command type.
   @see c2_net_test_cmd
 */
enum c2_net_test_cmd_type {
	C2_NET_TEST_CMD_INIT,
	C2_NET_TEST_CMD_INIT_DONE,
	C2_NET_TEST_CMD_START,
	C2_NET_TEST_CMD_START_ACK,
	C2_NET_TEST_CMD_STOP,
	C2_NET_TEST_CMD_STOP_ACK,
	C2_NET_TEST_CMD_FINISHED,
	C2_NET_TEST_CMD_FINISHED_ACK,
	C2_NET_TEST_CMD_NR,
};

/**
   C2_NET_TEST_CMD_*_ACK.
   @see c2_net_test_cmd
 */
struct c2_net_test_cmd_ack {
	int ntca_errno;
};

/**
   C2_NET_TEST_CMD_INIT.
   @see c2_net_test_cmd
 */
struct c2_net_test_cmd_init {
	/** node role */
	enum c2_net_test_role	 ntci_role;
	/** node type */
	enum c2_net_test_type	 ntci_type;
	/** number of test messages */
	unsigned long		 ntci_msg_nr;
	/** buffer size for bulk transfer */
	c2_bcount_t		 ntci_bulk_size;
	/** messages concurrency */
	uint32_t		 ntci_concurrency;
	/** endpoints list */
	struct c2_net_test_slist ntci_ep;
};

/**
   C2_NET_TEST_CMD_STOP.
   @see c2_net_test_cmd
 */
struct c2_net_test_cmd_stop {
	/** cancel the current operations */
	bool ntcs_cancel;
};

/**
   Command structure to exchange between console and clients or servers.
   @b WARNING: be sure to change cmd_xcode() and cmd_length()
   after changes to this structure.
 */
struct c2_net_test_cmd {
	/** command type */
	enum c2_net_test_cmd_type ntc_type;
	/** command structures */
	union {
		struct c2_net_test_cmd_ack  ntc_ack;
		struct c2_net_test_cmd_init ntc_init;
		struct c2_net_test_cmd_stop ntc_stop;
	};
	/**
	   Next fields will not be sent/received over the network.
	   They are used for error reporting etc.
	 */
	/** last unsuccesful operation -errno */
	int      ntc_errno;
	/** buffer status, c2_net_buffer_event.nbe_status in buffer callback */
	int      ntc_buf_status;
	/**
	   Do not send/receive this command.
	   It is set on every succesful c2_net_test_commands_send() and
	   c2_net_test_commands_wait() for command.
	 */
	bool     ntc_disabled;
	/** buffer index for c2_net_test_command_wait() */
	uint32_t ntc_buf_index;
};

/**
   Commands context.
 */
struct c2_net_test_cmd_ctx {
	/** network context for this command context */
	struct c2_net_test_network_ctx	 ntcc_net;
	/** array of commands */
	struct c2_net_test_cmd		*ntcc_cmd;
	/** number of commands in context */
	uint32_t			 ntcc_cmd_nr;
	/** used while waiting for buffer operations completion */
	struct c2_semaphore		 ntcc_sem;
};

/**
   Initialize network context to use with
   c2_net_test_cmd_send()/c2_net_test_cmd_wait().
   @param ctx commands context.
   @param cmd_ep endpoint for commands context.
   @param timeout_send timeout for message sending.
   @param timeout_recv timeout for message receing.
   @param ep_list endpoints list. Commands will be sent to/will be
		  expected from endpoints from this list.
   @return 0 (success)
   @return -EEXIST ep_list contains two equal strings
   @return -errno (failure)
 */
int c2_net_test_commands_init(struct c2_net_test_cmd_ctx *ctx,
			      char *cmd_ep,
			      c2_time_t timeout_send,
			      c2_time_t timeout_wait,
			      struct c2_net_test_slist *ep_list);
void c2_net_test_commands_fini(struct c2_net_test_cmd_ctx *ctx);

/**
   Invariant for c2_net_test_cmd_ctx.
   Time complexity is O(1).
 */
bool c2_net_test_commands_invariant(struct c2_net_test_cmd_ctx *ctx);

/**
   Send 'cmd' command to all endpoints from ctx. Block until MSG_SEND
   callback called for all endpoints or until timeout.
   @param ctx commands context.
   @param cmd command to send. Can be NULL - in this case commands are
	  taken from c2_net_test_cmd_ctx.ntcc_cmd, every command will be
	  sent to the corresponding endpoint from ctx (i-th command to
	  i-th endpoint).
   @return number of successfully sent commands.
 */
uint32_t c2_net_test_commands_send(struct c2_net_test_cmd_ctx *ctx,
				   struct c2_net_test_cmd *cmd);

/**
   Wait until command is received from all endpoints from ctx or until timeout.
   @param ctx commands context.
   @return number of successful received commands.
 */
uint32_t c2_net_test_commands_wait(struct c2_net_test_cmd_ctx *ctx);

/**
   C2_SET0() for all c2_net_test_cmd in context.
 */
void c2_net_test_commands_reset(struct c2_net_test_cmd_ctx *ctx);

/**
   Accessor to command by command index.
 */
struct c2_net_test_cmd *
c2_net_test_command(struct c2_net_test_cmd_ctx *ctx, uint32_t index);

/**
   @} end NetTestCommandsDFS
 */

#endif /*  __NET_TEST_COMMANDS_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
