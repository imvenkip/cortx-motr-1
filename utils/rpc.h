/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 10/03/2011
 */

#ifndef __COLIBRI_UTILS_RPC_H__
#define __COLIBRI_UTILS_RPC_H__

#include <sys/types.h> /* pid_t */

struct c2_rpc_helper_rpcmachine_ctx;
struct c2_rpc_helper_client_ctx;


/**
  Start RPC server in a separate process.

  @param rctx  Initialized rpc_machine context structure.
  @param client_addr Client's endpoint address.

  @retval PID of created server process
  @retval -errno on failure
*/
pid_t ut_rpc_server_start(struct c2_rpc_helper_rpcmachine_ctx *rctx,
			  const char *client_addr);

/**
  Stop running server process.

  @param server_pid  PID of server process

  @retval 0 on success
  @retval -errno on failure
*/
int ut_rpc_server_stop(pid_t server_pid);

/**
  Initializes client's rpc machine, creates a connection to a server and
  establishes an rpc session on top of it. Created session object can be set in
  an rpc item and used in c2_rpc_post().

  @param rctx  Initialized rpc_machine context structure.
  @param cctx  Initialized client context structure.
  @param rpc_session  Returns the rpc session object.

  @retval 0 on success
  @retval -errno on failure
*/
int ut_rpc_connect_to_server(struct c2_rpc_helper_rpcmachine_ctx *rctx,
			     struct c2_rpc_helper_client_ctx *cctx,
			     struct c2_rpc_session **rpc_session);

#endif /* __COLIBRI_UTILS_RPC_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
