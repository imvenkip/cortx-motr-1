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
#include "net/test/network.h" /* c2_net_test_ctx */

/**
   @defgroup NetTestCommandsDFS Colibri Network Benchmark Commands \
			      Detailed Functional Specification

   @see
   @ref net-test

   @{
 */

enum c2_net_test_command {
	C2_NET_TEST_CMD_INIT,
	C2_NET_TEST_CMD_INIT_DONE,
	C2_NET_TEST_CMD_START,
	C2_NET_TEST_CMD_START_ACK,
	C2_NET_TEST_CMD_STOP,
	C2_NET_TEST_CMD_STOP_ACK,
	C2_NET_TEST_CMD_FINISHED,
	C2_NET_TEST_CMD_FINISHED_ACK,
};

extern const struct c2_net_tm_callbacks     c2_net_test_commands_tm_cb;
extern const struct c2_net_buffer_callbacks c2_net_test_commands_buffer_cb;

/**
   Send 'cmd' command to all endpoints from ctx. Block until MSG_SEND
   callback called for all endpoints or until timeout.
   @param ctx c2_net_test network context. Should be initialized
   with c2_net_test_commands_tm_cb and c2_net_test_commands_buffer_cb.
   Should contain at least one endpoint.
   @param cmd command to send
   @param timeout timeout to wait.
   @return number of successful sent commands.
   @return -errno if error occurred.
 */
int c2_net_test_commands_send(struct c2_net_test_ctx ctx,
		enum c2_net_test_command cmd, c2_time_t timeout);

/**
   Wait until 'cmd' command is received from all endpoints from ctx.
   @param ctx c2_net_test network context. Should be initialized
   with c2_net_test_commands_tm_cb and c2_net_test_commands_buffer_cb.
   Should contain at least one endpoint.
   @param cmd command to wait.
   @param timeout timeout to wait. Set to 0 to disable timeout.
   @return number of successful received commands.
   @return -errno if error occurred.
 */
int c2_net_test_commands_wait(struct c2_net_test_ctx ctx,
		enum c2_net_test_command cmd, c2_time_t timeout);

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
