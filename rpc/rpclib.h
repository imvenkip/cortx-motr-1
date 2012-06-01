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

/**
   Create a buffer pool per net domain which to be shared by TM's in it.
   @pre ndom != NULL && app_pool != NULL
   @pre bufs_nr != 0
 */
int c2_rpc_net_buffer_pool_setup(struct c2_net_domain *ndom,
				 struct c2_net_buffer_pool *app_pool,
				 uint32_t bufs_nr, uint32_t tm_nr);

void c2_rpc_net_buffer_pool_cleanup(struct c2_net_buffer_pool *app_pool);

/**
 * Calculates the total number of buffers needed in network domain for
 * receive buffer pool.
 * @param len total Length of the TM's in a network domain
 * @param tms_nr    Number of TM's in the network domain
 */
static inline uint32_t c2_rpc_bufs_nr(uint32_t len, uint32_t tms_nr)
{
	return len +
	       /* It is used so that more than one free buffer is present
		* for each TM when tms_nr > 8.
		*/
	       max32u(tms_nr / 4, 1) +
	       /* It is added so that frequent low_threshold callbacks of
		* buffer pool can be reduced.
		*/
	       C2_NET_BUFFER_POOL_THRESHOLD;
}

/** Returns the maximum segment size of receive pool of network domain. */
static inline c2_bcount_t c2_rpc_max_seg_size(struct c2_net_domain *ndom)
{
	C2_PRE(ndom != NULL);

	return min64u(c2_net_domain_get_max_buffer_segment_size(ndom),
		      C2_SEG_SIZE);
}

/** Returns the maximum number of segments of receive pool of network domain. */
static inline uint32_t c2_rpc_max_segs_nr(struct c2_net_domain *ndom)
{
	C2_PRE(ndom != NULL);

	return c2_net_domain_get_max_buffer_size(ndom) /
	       c2_rpc_max_seg_size(ndom);
}

/** Returns the maximum RPC message size in the network domain. */
static inline c2_bcount_t c2_rpc_max_msg_size(struct c2_net_domain *ndom,
					      c2_bcount_t rpc_size)
{
	c2_bcount_t mbs;

	C2_PRE(ndom != NULL);

	mbs = c2_net_domain_get_max_buffer_size(ndom);
	return rpc_size != 0 ? rpc_size : mbs;
}

/**
 * Returns the maximum number of messages thet can be received in a buffer
 * of network domain.
 */
static inline uint32_t c2_rpc_max_recv_msgs(struct c2_net_domain *ndom,
					    c2_bcount_t rpc_size)
{
	C2_PRE(ndom != NULL);

	return c2_net_domain_get_max_buffer_size(ndom) /
	       c2_rpc_max_msg_size(ndom, rpc_size);
}

/**
 * It assigns the maximum RPC message size and maximum number of RPC messages
 * in a buffer to the RPC machine.
 * @pre rpc_mach != NULL && dom != NULL
 * @param colour Unique colour of each transfer machine.
 * @param msg_size Maximum RPC message size.
 * @param queue_len Minimum TM receive queue length.
 */
static inline void c2_rpc_machine_pre_init(struct c2_rpc_machine *rpc_mach,
					     struct c2_net_domain  *dom,
					     uint32_t		    colour,
					     c2_bcount_t	    msg_size,
					     uint32_t		    queue_len)
{
	C2_PRE(rpc_mach != NULL && dom != NULL);

	rpc_mach->rm_min_recv_size = c2_rpc_max_msg_size(dom, msg_size);
	rpc_mach->rm_max_recv_msgs = c2_rpc_max_recv_msgs(dom, msg_size);

	rpc_mach->rm_tm_colour		      = colour;
	rpc_mach->rm_tm_recv_queue_min_length = queue_len;

}

/**
 * Converts 127.0.0.1@tcp:12345:32:4 to local_ip@tcp:12345:32:4
 * and 127.0.0.1@oib:12345:32:4 to local_ip@oib:12345:32:4
 * @todo Needs to be removed once the alternate way to convert local addresses
 * for lnet is made available.
 */
int c2_lnet_local_addr_get(char *addr);

#endif /* __COLIBRI_RPC_RPCLIB_H__ */

