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

#ifndef __COLIBRI_RPC_SESSION_TYPES_H__

#define __COLIBRI_RPC_SESSION_TYPES_H__

#include <lib/cdefs.h>
#include <rpc/rpc_types.h>

/**
 @page rpc-session-types
*/

/**
 C2_SESSION_CREATE command
 */

/**
 parameters to the C2_SESSION_CREATE command
 */
struct c2_session_create_arg {
	/**
	client requested a new session
	*/
	struct c2_service_id	sca_client;
	/**
	server to accept connection
	*/
	struct c2_service_id	sca_server;
	/**
	maximal slot count handled by client
	*/
	uint32_t		sca_high_slot_id;
	/**
	maximal rpc size can be handled by client
	*/
	uint32_t		sca_max_rpc_size;
};

/**
 * server reply to SESSION_CREATE command.
 */
struct c2_session_create_ret {
	/**
	 status of operation.
	 if operation failed - not need a decoding reply
	*/
	int32_t error;
	/**
	server assigned session identifier
	*/
	struct c2_session_id sco_session_id;
	/**
	 maximal slot id (slot's count) assigned to the client
	*/
	uint32_t sco_high_slot_id;
	/**
	 maximal rpc size can be handle by client
	*/
	uint32_t sco_max_rpc_size;
};


/**
  C2_SESSION_DESTROY command
 */

/**
 argument to server side procedure
 */
struct c2_session_destroy_arg {
	/**
	 node to have addressed this request
	 */
	struct c2_service_id	da_service;
	/**
	session identifier to destroy
	*/
	struct c2_session_id	da_session;
};

/**
 reply to C2_SESSION_DESTOY command
 */
struct c2_session_destroy_ret {
	/**
	status of operation
	*/
	int32_t sda_errno;
};

#endif

