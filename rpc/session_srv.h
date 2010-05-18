/* -*- C -*- */
#ifndef __COLIBRI_RPC_SESSION_SRV_H__

#define __COLIBRI_RPC_SESSION_SRV_H__

#include "lib/cdefs.h"

#include "rpc/net_types.h"
#include "rpc/rpc_types.h"

/**
 @page rpc-srv-session  server side session handler.

@section rpc-srv-s-f functional specification

On server side, sessions is stored on database with BTREE structure and
structure c2_srv_session_key is used as primary key in the database,

@section rpc-srv-s-l Logical specification

when rpc service started it need to call c2_server_session_init
to intialize internal structures with correct data.


*/

/**
 key to store in session db,
 one record describe one slot in request handling.
 */
struct c2_server_session_key {
	/**
	 client which send a request
	 */
	struct c2_service_id	ssk_client;
	/**
	 session to handle that request
	 */
	struct c2_session_id	ssk_sess;
	/**
	 slot to handle that request
	 */
	c2_slot_t		skk_slotid;
};


/**
 data to store in session db
*/
struct c2_server_session_data {
	/**
	 sequence in the slot
	 */
	c2_seq_t	ssd_sequence;
};

/**
 request ordering enumeration
 */
enum c2_request_order {
	/**
	 request out of order
	 */
	REQ_ORD_BAD,
	/**
	 normal request in expected order,
	 sequence id is greater last used by one.
	 */
	REQ_ORD_NORMAL,
	/**
	 resend of already handled request.
	 request sequence is same prevoisly hanlded
	 */
	REQ_ORD_RESEND,
};

/**
 check request order in given slot info.

 @param sess - server side session info
 @param cli_seq - client provided info in sequence operation.

 @return enum request code to describe request status.
 */
enum c2_request_order c2_check_request(const struct c2_srv_session_data *sess,
					const struct c2_session_sequence_args *cli_seq);

/**
 open or create session for the server
*/
int c2_server_session_init(struct c2_rpc_server *srv);

/**
 close session db and free allocated resources
*/
void c2_server_session_fini(struct c2_rpc_server *srv);

/**
 create new session on a server.
 
 @param srv - server to create new session
 @param sess - ponter to new created session, with 2 references.
               need a call c2_srv_session release after using.

 @retval 0 - creation OK
 @retval -ENOMEM not have enogth memory
 */
int c2_server_session_create(struct c2_rpc_server *srv,
			     struct c2_session_id **sess);

/**
 unlink session from a list and release one reference
 */
void c2_server_session_delete(const struct c2_rpc_server *srv,
			      const struct c2_session_id *sess);

/**
 find session by session id

 @param srv - server to find session
 @param ss_id - session identifier
 
 @retval NULL - session not exist or unlinked
 @retval !NULL - session with that identifier
*/
struct c2_server_session_data *
c2_server_session_lookup(const struct c2_rpc_server *srv,
			 const c2_session_id *ss_id);

#endif
