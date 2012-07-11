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
#include "net/test/ringbuf.h"		/* c2_net_test_ringbuf */
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
	size_t			 ntci_concurrency;
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
   @b WARNING: be sure to change cmd_xcode() after changes to this structure.
   @note c2_net_test_cmd.ntc_ep_index and c2_net_test_cmd.ntc_buf_index
   will not be sent/received over the network.
 */
struct c2_net_test_cmd {
	/** command type */
	enum c2_net_test_cmd_type ntc_type;
	/** command structures */
	/** @todo add others commands */
	union {
		struct c2_net_test_cmd_ack  ntc_ack;
		struct c2_net_test_cmd_init ntc_init;
		struct c2_net_test_cmd_stop ntc_stop;
	};
	/**
	   Endpoint index in commands context.
	   Set in c2_net_test_commands_recv().
	   Used in c2_net_test_commands_send().
	   Will be set to -1 in c2_net_test_commands_recv()
	   if endpoint isn't present in commands context endpoints list.
	 */
	ssize_t ntc_ep_index;
	/** buffer index. set in c2_net_test_commands_recv() */
	size_t  ntc_buf_index;
};

struct c2_net_test_cmd_buf_status {
	/**
	   get() in message receive callback.
	   put() in c2_net_test_commands_recv().
	 */
	struct c2_net_end_point *ntcbs_ep;
	/** buffer status, c2_net_buffer_event.nbe_status in buffer callback */
	int			 ntcbs_buf_status;
	/** buffer was added to the receive queue */
	bool			 ntcbs_in_recv_queue;
};

struct c2_net_test_cmd_ctx;

/** 'Command sent' callback. */
typedef void (*c2_net_test_commands_send_cb_t)(struct c2_net_test_cmd_ctx *ctx,
					       size_t ep_index,
					       int buf_status);

/**
   Commands context.
 */
struct c2_net_test_cmd_ctx {
	/** network context for this command context */
	struct c2_net_test_network_ctx	   ntcc_net;
	/**
	   Ring buffer for receive queue.
	   c2_net_test_ringbuf_put() in message receive callback.
	   c2_net_test_ringbuf_get() in c2_net_test_commands_recv().
	 */
	struct c2_net_test_ringbuf	   ntcc_rb;
	/** number of commands in context */
	size_t				   ntcc_ep_nr;
	/** used while waiting for buffer operations completion */
	/** @todo problem with semaphore max value can be here */
	/**
	   c2_semaphore_up() in message send callback.
	   c2_semaphore_down() in c2_net_test_commands_send_wait_all().
	 */
	struct c2_semaphore		   ntcc_sem_send;
	/**
	   c2_semaphore_up() in message recv callback.
	   c2_semaphore_timeddown() in c2_net_test_commands_recv().
	 */
	struct c2_semaphore		   ntcc_sem_recv;
	/** Called from message send callback */
	c2_net_test_commands_send_cb_t	   ntcc_send_cb;
	/**
	   Number of sent commands.
	   Resets to 0 on every call to c2_net_test_commands_wait_all().
	 */
	struct c2_atomic64		   ntcc_send_nr;
	/**
	   Updated in message send/recv callbacks.
	 */
	struct c2_net_test_cmd_buf_status *ntcc_buf_status;
};

/**
   Initialize commands context.
   @param ctx commands context.
   @param cmd_ep endpoint for commands context.
   @param send_timeout timeout for message sending.
   @param send_cb 'Command sent' callback. Can be NULL.
   @param ep_list endpoints list. Commands will be sent to/will be
		  expected from endpoints from this list.
   @return 0 (success)
   @return -EEXIST ep_list contains two equal strings
   @return -errno (failure)
   @note
   - buffers for message sending/receiving will be allocated here,
     two buffers per endpoint;
   - all buffers will have C2_NET_TEST_CMD_SIZE_MAX size;
   - all buffers for receiving commands will be added to receive queue here;
   - buffers will not be automatically added to receive queue after
     call to c2_net_test_commands_recv();
   - c2_net_test_commands_recv() can allocate resources while decoding
     command from buffer, so c2_net_test_received_free() must be called
     for command after succesful c2_net_test_commads_recv().
 */
int c2_net_test_commands_init(struct c2_net_test_cmd_ctx *ctx,
			      char *cmd_ep,
			      c2_time_t send_timeout,
			      c2_net_test_commands_send_cb_t send_cb,
			      struct c2_net_test_slist *ep_list);
void c2_net_test_commands_fini(struct c2_net_test_cmd_ctx *ctx);

/**
   Invariant for c2_net_test_cmd_ctx.
   Time complexity is O(1).
 */
bool c2_net_test_commands_invariant(struct c2_net_test_cmd_ctx *ctx);

/**
   Send command.
   @param ctx Commands context.
   @param cmd Command to send. cmd->ntc_ep_index should be set to valid
	      endpoint index in the commands context.
 */
int c2_net_test_commands_send(struct c2_net_test_cmd_ctx *ctx,
			      struct c2_net_test_cmd *cmd);
/**
   Wait until all 'command send' callbacks executed for every sent command.
 */
void c2_net_test_commands_send_wait_all(struct c2_net_test_cmd_ctx *ctx);

/**
   Receive command.
   @param ctx Commands context.
   @param cmd Received buffer will be decoded to this structure.
	      c2_net_test_received_free() should be called for cmd to free
	      resources that can be allocated while decoding.
   @param deadline Functon will wait until deadline reached. Absolute time.

   @note cmd->ntc_buf_index will be set to buffer index with received command.
   This buffer will be removed from receive queue and should be added using
   c2_net_test_commands_recv_enqueue().
   @see c2_net_test_commands_init().
 */
int c2_net_test_commands_recv(struct c2_net_test_cmd_ctx *ctx,
			      struct c2_net_test_cmd *cmd,
			      c2_time_t deadline);
/**
   Add commands context buffer to commands receive queue.
   @see c2_net_test_commands_recv().
 */
int c2_net_test_commands_recv_enqueue(struct c2_net_test_cmd_ctx *ctx,
				      size_t buf_index);
/**
   Free received command resources.
   @see c2_net_test_commands_recv().
 */
void c2_net_test_received_free(struct c2_net_test_cmd *cmd);

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
