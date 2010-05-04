/* -*- C -*- */
#ifndef _RPC_SVC_H_

#define _RPC_SVC_H_

/**
 @page rpc-svc server side handlers.
 
 server side is reponsible to handle commands:
 SESSION_CREATE,
 SESSION_DESTROY,
 SESSION_COMPOUND,
 SESSION_ADJUST.

 All these commands need to be listed in enum c2_session_cmd
*/

/**
 server handler for the SESSION_CREATE command.
 create new session on server and connect session into session list.

 @param in  - structure with arguments from client to creation session.
 @param out - structure returned to client

 @retval true  - need send a reply
 @retval false - not need send a reply - some generic error is hit.
 */
bool c2_session_create_svc(const struct session_create_arg *in,
			   struct session_create_ret *out);

/**
 server handler for the SESSION_DESTROY cmd.
 destroy session on server side.
 
 @param in  - session id + parameters to destroy session from client
 @param out - resulting info to send to client
 
 @retval true  - need send a reply
 @retval false - not need send a reply - some generic error is hit.
 */
bool c2_session_destroy_svc(const struct session_destroy_arg *in,
			    struct session_destroy_out *out);

/**
 SESSION_COMPOUND command handler

 @param in  - structure with compound header and array of operations
 @param out - resulting structure to send to client

 @retval true  - need send a reply
 @retval false - not need send a reply - some generic error is hit.
 */
bool c2_session_compound_svc(const struct session_compound_arg *in,
			     struct session_compound_reply *out);

#endif
