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

#ifndef __COLIBRI_RPC_RPC_HELPER_H__
#define __COLIBRI_RPC_RPC_HELPER_H__

#include "lib/types.h"
#include "rpc/rpccore.h" /* struct c2_rpcmachine, c2_rpc_item */
#include "rpc/session.h" /* struct c2_rpc_conn, c2_rpc_session */
#include "db/db.h"       /* struct c2_dbenv */
#include "cob/cob.h"     /* struct c2_cob_domain */
#include "net/net.h"     /* struct c2_net_end_point */


struct c2_net_xprt;
struct c2_net_domain;
struct c2_reqh;

/**
 * RPC context structure.
 * Contains all required data to initialize RPC client and server.
 */
struct c2_rpc_ctx {

	/**
	 * Input parameters.
	 *
	 * They are initialized and filled in by a caller of
	 * c2_rpc_server_init() and c2_rpc_client_init().
	 */

        struct c2_net_domain    *rx_net_dom;

	/** Can be NULL. In this case a default reqh will be allocated and
	 * initialized by c2_rpc_(server|client)_init() */
	struct c2_reqh          *rx_reqh;

	/** Transport specific local address */
	const char              *rx_local_addr;

	/** Transport specific remote address */
	const char              *rx_remote_addr;

	/** Name of database used by the RPC machine */
	const char              *rx_db_name;

	/** Identity of cob used by the RPC machine */
	uint32_t                rx_cob_dom_id;

	/** Number of session slots */
	uint32_t                rx_nr_slots;

	uint64_t                rx_max_rpcs_in_flight;

	/** Time in seconds after which connection/session
	 *  establishment is aborted */
	uint32_t                rx_timeout_s;

	/**
	 * Output parameters.
	 *
	 * They are initialized and filled in by c2_rpc_server_init() and
	 * c2_rpc_client_init().
	 */

        struct c2_dbenv         rx_dbenv;
        struct c2_cob_domain    rx_cob_dom;
	struct c2_rpcmachine    rx_rpc_machine;
        struct c2_net_end_point	*rx_remote_ep;
        struct c2_rpc_conn      rx_connection;
        struct c2_rpc_session   rx_session;
};

/**
  Starts RPC server.

  @param rctx  Initialized rpc context structure.
*/
int c2_rpc_server_init(struct c2_rpc_ctx *rctx);

/**
  Starts client's rpc machine. Creates a connection to a server and establishes
  an rpc session on top of it.  Created session object can be set in an rpc item
  and used in c2_rpc_post().

  @param cctx  Initialized rpc context structure.
*/
int c2_rpc_client_init(struct c2_rpc_ctx *rctx);

/**
  Make an RPC call to a server, blocking for a reply if desired.

  @param item The rpc item to send.  Presumably ri_reply will hold the reply upon
              successful return.
  @param rpc_session The session to be used for the client call.
  @param timeout_s Timeout in seconds.  0 implies don't wait for a reply.
*/
int c2_rpc_client_call(struct c2_fop *fop, struct c2_rpc_session *session,
		       uint32_t timeout_s);

/**
  Stops RPC server.

  @param rctx  Initialized rpc context structure.
*/
void c2_rpc_server_fini(struct c2_rpc_ctx *rctx);

/**
  Terminates RPC session and connection with server and finalize client's RPC
  machine.

  @param rctx  Initialized rpc context structure.
*/
int c2_rpc_client_fini(struct c2_rpc_ctx *rctx);

#endif /* __COLIBRI_RPC_RPC_HELPER_H__ */

