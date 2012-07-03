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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 09/28/2011
 */

#ifndef __COLIBRI_RPC_RPCLIB_H__
#define __COLIBRI_RPC_RPCLIB_H__

#ifndef __KERNEL__
#include <stdio.h> /* FILE */
#endif

#include "rpc/rpc2.h"    /* struct c2_rpc_machine, c2_rpc_item */
#include "rpc/session.h" /* struct c2_rpc_conn, c2_rpc_session */
#include "db/db.h"       /* struct c2_dbenv */
#include "cob/cob.h"     /* struct c2_cob_domain */
#include "net/net.h"     /* struct c2_net_end_point */
#include "net/buffer_pool.h"

#ifndef __KERNEL__
#include "colibri/colibri_setup.h" /* struct c2_colibri */
#endif


#ifndef __KERNEL__
struct c2_reqh;
struct c2_reqh_service_type;

/**
 * RPC server context structure.
 *
 * Contains all required data to initialize an RPC server,
 * using colibri-setup API.
 */
struct c2_rpc_server_ctx {

	/** a pointer to array of transports, which can be used by server */
	struct c2_net_xprt          **rsx_xprts;
	/** number of transports in array */
	int                         rsx_xprts_nr;

	/**
	 * ARGV-like array of CLI options to configure colibri-setup, which is
	 * passed to c2_cs_setup_env()
	 */
	char                        **rsx_argv;
	/** number of elements in rsx_argv array */
	int                         rsx_argc;

	/** a pointer to array of service types, which can be used by server */
	struct c2_reqh_service_type **rsx_service_types;
	/** number of service types in array */
	int                         rsx_service_types_nr;

	const char                  *rsx_log_file_name;

	/** an embedded colibri context structure */
	struct c2_colibri           rsx_colibri_ctx;

	/**
	 * this is an internal variable, which is used by c2_rpc_server_stop()
	 * to close log file; it should not be initialized by a caller
	 */
	FILE                        *rsx_log_file;
};

/**
  Starts server's rpc machine.

  @param sctx  Initialized rpc context structure.

  @pre sctx->rcx_dbenv and rctx->rcx_cob_dom are initialized
*/
int c2_rpc_server_start(struct c2_rpc_server_ctx *sctx);

/**
  Stops RPC server.

  @param sctx  Initialized rpc context structure.
*/
void c2_rpc_server_stop(struct c2_rpc_server_ctx *sctx);
#endif

struct c2_net_xprt;
struct c2_net_domain;

/**
 * RPC client context structure.
 *
 * Contains all required data to initialize an RPC client and connect to server.
 */
struct c2_rpc_client_ctx {

	/**
	 * Input parameters.
	 *
	 * They are initialized and filled in by a caller of
	 * c2_rpc_server_start() and c2_rpc_client_stop().
	 */

	/**
	 * A pointer to net domain struct which will be initialized and used by
	 * c2_rpc_client_start()
	 */
	struct c2_net_domain      *rcx_net_dom;

	/** Transport specific local address (client's address) */
	const char                *rcx_local_addr;

	/** Transport specific remote address (server's address) */
	const char                *rcx_remote_addr;

	/** Name of database used by the RPC machine */
	const char                *rcx_db_name;

	/**
	 * A pointer to dbenv struct which will be initialized and used by
	 * c2_rpc_client_start()
	 */
	struct c2_dbenv           *rcx_dbenv;

	/** Identity of cob used by the RPC machine */
	uint32_t                   rcx_cob_dom_id;

	/**
	 * A pointer to cob domain struct which will be initialized and used by
	 * c2_rpc_client_start()
	 */
	struct c2_cob_domain      *rcx_cob_dom;

	/** Number of session slots */
	uint32_t		   rcx_nr_slots;

	uint64_t		   rcx_max_rpcs_in_flight;

	/**
	 * Time in seconds after which connection/session
	 * establishment is aborted.
	 */
	uint32_t		   rcx_timeout_s;

	/**
	 * Output parameters.
	 *
	 * They are initialized and filled in by c2_rpc_server_init() and
	 * c2_rpc_client_start().
	 */

	struct c2_rpc_machine	   rcx_rpc_machine;
	struct c2_net_end_point	  *rcx_remote_ep;
	struct c2_rpc_conn	   rcx_connection;
	struct c2_rpc_session	   rcx_session;

	/** Buffer pool used to provision TM receive queue. */
	struct c2_net_buffer_pool  rcx_buffer_pool;

	/**
	 * List of buffer pools in colibri context.
	 * @see c2_cs_buffer_pool::cs_bp_linkage
	 */
        uint32_t		   rcx_recv_queue_min_length;

	/** Maximum RPC recive buffer size. */
        uint32_t		   rcx_max_rpc_msg_size;
};

/**
  Starts client's rpc machine. Creates a connection to a server and establishes
  an rpc session on top of it.  Created session object can be set in an rpc item
  and used in c2_rpc_post().

  @param cctx  Initialized rpc context structure.

  @pre cctx->rcx_dbenv and rctx->rcx_cob_dom are initialized
*/
int c2_rpc_client_start(struct c2_rpc_client_ctx *cctx);

/**
  Make an RPC call to a server, blocking for a reply if desired.

  @param item        The rpc item to send.  Presumably ri_reply will hold the
                     reply upon successful return.
  @param rpc_session The session to be used for the client call.
  @param ri_ops      Pointer to RPC item ops structure.
  @param timeout_s   Timeout in seconds.  0 implies don't wait for a reply.
*/
int c2_rpc_client_call(struct c2_fop *fop, struct c2_rpc_session *session,
		       const struct c2_rpc_item_ops *ri_ops, uint32_t timeout_s);

/**
  Terminates RPC session and connection with server and finalize client's RPC
  machine.

  @param cctx  Initialized rpc context structure.
*/
int c2_rpc_client_stop(struct c2_rpc_client_ctx *cctx);

#endif /* __COLIBRI_RPC_RPCLIB_H__ */

