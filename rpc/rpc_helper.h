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
  Initialize an RPC machine with a fake request handler that will invoke a set
  of user supplied FOMs.

  @param xprt Specify transport address
  @param addr_local  Transport specific local address
  @param db_name  Name of database used by the RPC machine
  @param cob_domain_id  Identity of cob used by the RPC machine
  [@param fake request handler arguments]
  @param rpc_machine Pass in an rpc machine to be initialized.
*/
int c2_rpc_helper_init_machine(struct c2_net_xprt *xprt, const char *addr_local,
			       const char *db_name, uint32_t cob_domain_id,
			       struct c2_reqh *request_handler,
			       struct c2_rpcmachine *rpc_machine);

/**
  Creates a session to a server which can be set in an rpc item and used in
  c2_rpc_post().

  @param rpc_machine Initialized RPC machine
  @param addr_remote Remote server address
  @param nr_slots Number of session slots
  @param timeout_s  Time in seconds after which session establishment is aborted.
  @param rpc_session  Returns the rpc session object.
*/
int c2_rpc_helper_client_connect(struct c2_rpcmachine *rpc_machine,
				 const char *addr_remote, uint32_t nr_slots,
				 uint32_t timeout_s,
				 struct c2_rpc_session **rpc_session);

/**
 Make an RPC call to a server, blocking for a reply if desired.

 @param item The rpc item to send.  Presumably ri_reply will hold the reply upon
             successful return.
 @param rpc_session The session to be used for the client call.
 @param timeout_s Timeout in seconds.  0 implies don't wait for a reply.
*/
int c2_rpc_helper_client_call(struct c2_rpc_item *item,
			      struct c2_rpc_session *rpc_session,
			      uint32_t timeout_s);

/** Clean up all allocated data structures, associated with rpc_machine */
int c2_rpc_helper_cleanup(struct c2_rpcmachine *rpc_machine);

#endif /* __COLIBRI_RPC_HELPER_H__ */

