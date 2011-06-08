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
 * Original author: Alexey Lyashkov
 * Original creation date: 04/22/2010
 */
#ifndef __COLIBRI_RPC_SVC_H__

#define __COLIBRI_RPC_SVC_H__

#include <lib/cdefs.h>

/**
 @page rpc-svc server side handlers.
 
 server side is reponsible to handle commands:
 SESSION_CREATE,
 SESSION_DESTROY,

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
bool c2_session_create_svc(const struct c2_rpc_op *op, void *in, void **out);

/**
 server handler for the SESSION_DESTROY cmd.
 destroy session on server side.
 
 @param in  - session id + parameters to destroy session from client
 @param out - resulting info to send to client
 
 @retval true  - need send a reply
 @retval false - not need send a reply - some generic error is hit.
 */
bool c2_session_destroy_svc(const struct c2_rpc_op *op, void *in, void **out);

#endif
