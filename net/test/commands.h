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

#include "net/net.h"
#include "net/test/network.h"		/* c2_net_test_network_ctx */
#include "net/test/node_config.h"	/* c2_net_test_role */

/**
   @defgroup NetTestCommandsDFS Colibri Network Benchmark Commands \
			      Detailed Functional Specification

   @see
   @ref net-test

   @{
 */

/**
   String list.
 */
struct c2_net_test_slist {
	/**
	   Number of strings in the list. If it is 0, other fields are
	   not valid.
	 */
	int    ntsl_nr;
	/**
	   Array of pointers to strings.
	 */
	char **ntsl_list;
	/**
	   Single array with '\0'-separated strings (one after another).
	   ntsl_list contains the pointers to a strings in this array.
	 */
	char  *ntsl_str;
};

/**
   Command type.
   @see c2_net_test_command
 */
enum c2_net_test_command_type {
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
   @see c2_net_test_command
 */
struct c2_net_test_command_ack {
	int ntca_errno;
};

/**
   C2_NET_TEST_CMD_INIT.
   @see c2_net_test_command
 */
struct c2_net_test_command_init {
	/** node role */
	enum c2_net_test_role	 ntci_role;
	/** node type */
	enum c2_net_test_type	 ntci_type;
	/** buffer size for bulk transfer */
	c2_bcount_t		 ntci_bulk_size;
	/** messages concurrency */
	uint32_t		 ntci_concurrency;
	/** endpoints list */
	struct c2_net_test_slist ntci_ep;
	/** console endpoint */
	char			*ntci_ep_console;
};

/**
   C2_NET_TEST_CMD_FINI.
   @see c2_net_test_command
 */
struct c2_net_test_command_fini {
	/** cancel the current operations */
	bool ntcf_cancel;
};

/**
   Command structure to exchange between console and clients or servers.
   @b WARNING: be sure to change command_encode(), command_decode() and
   command_size_max() after changes to this structure.
 */
struct c2_net_test_command {
	int ntc_errno;
	enum c2_net_test_command_type ntc_type;
	union {
		struct c2_net_test_command_ack ntc_ack;
		struct c2_net_test_command_init ntc_init;
		struct c2_net_test_command_init ntc_fini;
	};
};

/**
   Commands context.
 */
struct c2_net_test_command_ctx {
	struct c2_net_test_network_ctx ntcc_net;
	struct c2_net_test_command    *ntcc_cmd;
	uint32_t		       ntcc_cmd_nr;
};

/**
   Initialize network context to use with c2_net_test_command_send/
   c2_net_test_command_recv.
 */
int c2_net_test_command_init(struct c2_net_test_command_ctx *ctx,
		char *cmd_ep,
		c2_time_t timeout_send,
		c2_time_t timeout_wait,
		struct c2_net_test_slist ep_list);
void c2_net_test_command_fini(struct c2_net_test_command_ctx *ctx);
bool c2_net_test_command_invariant(struct c2_net_test_command_ctx *ctx);

/**
   Send 'cmd' command to all endpoints from ctx. Block until MSG_SEND
   callback called for all endpoints or until timeout.
   @param ctx commands context.
   @param cmd command to send
   @return number of successful sent commands.
 */
int c2_net_test_command_send_single(struct c2_net_test_command_ctx *ctx,
		struct c2_net_test_command *cmd);

/**
   Send corresponding command to all endpoints from ctx. Block until MSG_SEND
   callback called for all endpoints or until timeout.
   @param ctx commands context.
   @return number of successful sent commands.
 */
int c2_net_test_command_send(struct c2_net_test_command_ctx *ctx);

/**
   Wait until command is received from all endpoints from ctx.
   @param ctx commands context.
   @return number of successful received commands.
 */
int c2_net_test_command_wait(struct c2_net_test_command_ctx *ctx);

#if 0
/**
   Copy command to all commands in the command context.
   WARNING: all corresponding pointers in command context commands will
   have the same value as in command, given to this function. There will
   be no additional allocation/deallocation, just copying the values.
 */
int c2_net_test_command_scatter(struct c2_net_test_command_ctx *ctx,
		struct c2_net_test_command *cmd);
#endif

/**
   Accessor to command by command index.
 */
struct c2_net_test_command *c2_net_test_command_cmd(
		struct c2_net_test_command_ctx *ctx, uint32_t index);

/**
   Initialize string list from a string and a delimiter.
   XXX
 */
int c2_net_test_slist_init(struct c2_net_test_slist *slist,
		char *str, char delim);
void c2_net_test_slist_fini(struct c2_net_test_slist *slist);

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
