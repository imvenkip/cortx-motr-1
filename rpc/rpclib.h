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

#pragma once

#ifndef __MERO_RPC_RPCLIB_H__
#define __MERO_RPC_RPCLIB_H__

#ifndef __KERNEL__
#  include <stdio.h> /* FILE */
#endif

#include "rpc/rpc.h"
#include "db/db.h"       /* struct m0_dbenv */
#include "cob/cob.h"     /* struct m0_cob_domain */
#include "net/net.h"     /* struct m0_net_end_point */
#include "net/buffer_pool.h"

#ifndef __KERNEL__
#  include "mero/setup.h" /* struct m0_mero */
#endif

struct m0_fop;

#ifndef __KERNEL__
struct m0_reqh;
struct m0_reqh_service_type;

/**
 * RPC server context structure.
 *
 * Contains all required data to initialize an RPC server,
 * using mero-setup API.
 */
struct m0_rpc_server_ctx {

	/** a pointer to array of transports, which can be used by server */
	struct m0_net_xprt          **rsx_xprts;
	/** number of transports in array */
	int                           rsx_xprts_nr;

	/**
	 * ARGV-like array of CLI options to configure mero-setup, which is
	 * passed to m0_cs_setup_env()
	 */
	char                        **rsx_argv;
	/** number of elements in rsx_argv array */
	int                           rsx_argc;

	/** a pointer to array of service types, which can be used by server */
	struct m0_reqh_service_type **rsx_service_types;
	/** number of service types in array */
	int                           rsx_service_types_nr;

	const char                   *rsx_log_file_name;

	/** an embedded mero context structure */
	struct m0_mero                rsx_mero_ctx;

	/**
	 * this is an internal variable, which is used by m0_rpc_server_stop()
	 * to close log file; it should not be initialized by a caller
	 */
	FILE                         *rsx_log_file;
};

/**
  Starts server's rpc machine.

  @param sctx  Initialized rpc context structure.

  @pre sctx->rcx_dbenv and rctx->rcx_cob_dom are initialized
*/
int m0_rpc_server_start(struct m0_rpc_server_ctx *sctx);

/**
  Stops RPC server.

  @param sctx  Initialized rpc context structure.
*/
void m0_rpc_server_stop(struct m0_rpc_server_ctx *sctx);

M0_INTERNAL struct m0_rpc_machine *
m0_rpc_server_ctx_get_rmachine(struct m0_rpc_server_ctx *sctx);

#endif

struct m0_net_xprt;
struct m0_net_domain;

/**
 * RPC client context structure.
 *
 * Contains all required data to initialize an RPC client and connect to server.
 */
struct m0_rpc_client_ctx {

	/**
	 * Input parameters.
	 *
	 * They are initialized and filled in by a caller of
	 * m0_rpc_client_start().
	 */

	/**
	 * A pointer to net domain struct which will be initialized and used by
	 * m0_rpc_client_start()
	 */
	struct m0_net_domain      *rcx_net_dom;

	/** Transport specific local address (client's address) */
	const char                *rcx_local_addr;

	/** Transport specific remote address (server's address) */
	const char                *rcx_remote_addr;

	/** Name of database used by the RPC machine */
	const char                *rcx_db_name;

	/**
	 * A pointer to dbenv struct which will be initialized and used by
	 * m0_rpc_client_start()
	 */
	struct m0_dbenv           *rcx_dbenv;

	/** Identity of cob used by the RPC machine */
	uint32_t                   rcx_cob_dom_id;

	/**
	 * A pointer to cob domain struct which will be initialized and used by
	 * m0_rpc_client_start()
	 */
	struct m0_cob_domain      *rcx_cob_dom;

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
	 * They are initialized and filled in by m0_rpc_client_start().
	 */

	struct m0_rpc_machine	   rcx_rpc_machine;
	struct m0_rpc_conn	   rcx_connection;
	struct m0_rpc_session	   rcx_session;

	/** Buffer pool used to provision TM receive queue. */
	struct m0_net_buffer_pool  rcx_buffer_pool;

	/** Minimum number of buffers in TM receive queue. */
        uint32_t		   rcx_recv_queue_min_length;

	/** Maximum RPC recive buffer size. */
        uint32_t		   rcx_max_rpc_msg_size;
};

/**
 * Establishes RPC connection and creates a session.
 *
 * @param[out] conn
 * @param[out] session
 * @param[in]  rpc_mach
 * @param[in]  remote_addr
 * @param[in]  max_rpcs_in_flight
 * @param[in]  nr_slots
 */
M0_INTERNAL int m0_rpc_client_connect(struct m0_rpc_conn    *conn,
				      struct m0_rpc_session *session,
				      struct m0_rpc_machine *rpc_mach,
				      const char            *remote_addr,
				      uint64_t               max_rpcs_in_flight,
				      uint32_t               nr_slots,
				      uint32_t               rpc_timeout_sec);

/**
  Starts client's rpc machine. Creates a connection to a server and establishes
  an rpc session on top of it.  Created session object can be set in an rpc item
  and used in m0_rpc_post().

  @param cctx  Initialized rpc context structure.

  @pre cctx->rcx_dbenv and rctx->rcx_cob_dom are initialized
*/
int m0_rpc_client_start(struct m0_rpc_client_ctx *cctx);

/**
  Make an RPC call to a server, blocking for a reply if desired.

  @param item        The rpc item to send.  Presumably ri_reply will hold the
                     reply upon successful return.
  @param rpc_session The session to be used for the client call.
  @param ri_ops      Pointer to RPC item ops structure.
  @param deadline    Absolute time after which formation should send the fop
		     as soon as possible. deadline should be 0 if fop shouldn't
		     wait in formation queue and should be sent immediately.
  @param timeout   Absolute operation timeout.
*/
int m0_rpc_client_call(struct m0_fop *fop,
		       struct m0_rpc_session *session,
		       const struct m0_rpc_item_ops *ri_ops,
		       m0_time_t deadline, m0_time_t timeout);

/**
  Terminates RPC session and connection with server and finalize client's RPC
  machine.

  @param cctx  Initialized rpc context structure.
*/
int m0_rpc_client_stop(struct m0_rpc_client_ctx *cctx);

#endif /* __MERO_RPC_RPCLIB_H__ */

