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
 * Original creation date: 09/28/2011
 */

#ifndef __COLIBRI_RPC_HELPER_H__
#define __COLIBRI_RPC_HELPER_H__

#include "lib/types.h"


struct c2_net_xprt;
struct c2_rpcmachine;
struct c2_reqh;
struct c2_rpc_session;
struct c2_rpc_item;

/**
  A context structure, which contains enough information to initialize
  an RPC machine.

  It's used as an input parameter for c2_rpc_helper_init_machine()
*/
struct c2_rpc_helper_rpcmachine_ctx {
	/** Specify transport address */
	struct c2_net_xprt  *xprt;
	/** Transport specific local address */
	const char          *local_addr;
	/** Name of database used by the RPC machine */
	const char          *db_name;
	/** Identity of cob used by the RPC machine */
	uint32_t            cob_domain_id;
	/** Pass in an rpc machine to be initialized */
	struct c2_reqh      *request_handler;
};

/**
  A context structure, which contains enough information to initialize
  an RPC session.

  It's used as an input parameter for c2_rpc_helper_client_connect()
*/
struct c2_rpc_helper_client_ctx {
	/** Initialized RPC machine */
	struct c2_rpcmachine  *rpc_machine;
	/** Remote server address */
	const char            *remote_addr;
	/** Number of session slots */
	uint32_t              nr_slots;
	/** Time in seconds after which session establishment is aborted */
	uint32_t              timeout_s;
};

/**
  Initialize an RPC machine with a request handler that will invoke a set
  of user supplied FOMs.

  @param rctx  Initialized rpc_machine context structure.
  @param rpc_machine Pass in an rpc machine to be initialized.

  @retval 0 on success
  @retval -errno on failure
*/
int c2_rpc_helper_init_machine(struct c2_rpc_helper_rpcmachine_ctx *rctx,
			       struct c2_rpcmachine *rpc_machine);

/**
  Creates a connection to a server and establishes an rpc session on top of it.
  Created session object can be set in an rpc item and used in c2_rpc_post().

  @param cctx  Initialized client context structure.
  @param rpc_session  Returns the rpc session object.

  @retval 0 on success
  @retval -errno on failure
*/
int c2_rpc_helper_client_connect(struct c2_rpc_helper_client_ctx *cctx,
				 struct c2_rpc_session **rpc_session);

/**
  Make an RPC call to a server, blocking for a reply if desired.

  @param item The rpc item to send.  Presumably ri_reply will hold the reply upon
              successful return.
  @param rpc_session The session to be used for the client call.
  @param timeout_s Timeout in seconds.  0 implies don't wait for a reply.

  @retval 0 on success
  @retval -errno on failure
*/
int c2_rpc_helper_client_call(struct c2_fop *fop, struct c2_rpc_session *session,
			      uint32_t timeout_s);

/** Clean up all allocated data structures, associated with rpc_machine

  @param rpc_machine The rpc machine object for which cleanup is performed.

  @retval 0 on success
  @retval -errno on failure
*/
int c2_rpc_helper_cleanup(struct c2_rpcmachine *rpc_machine);

#endif /* __COLIBRI_RPC_HELPER_H__ */

