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
 * Original author: Alexey Lyashkov <Alexey_Lyashkov@xyratex.com>
 * Original creation date: 05/12/2010
 */

#pragma once

#ifndef __COLIBRI_RPC_COMPOUND_SRV_H__

#define __COLIBRI_RPC_COMPOUND_SRV_H__

#include <net/net.h>
#include <lib/cache.h>
#include <rpc/compound_types.h>

/**
 @section rpc-compound-server functions is related to server side
 */

/**
 handlers for operations encapsulated in compound request
 */
struct c2_compound_ops {
	/**
	 operation identifier
	 */
	c2_compound_op		co_op;
	/**
	 convert argument from network to host order
	 */
	c2_xdrproc_t		co_arg;
	/**
	 convert reply from host to network order
	 */
	c2_xdrproc_t		co_ret;
	/**
	 operation handler function.
	 handler must allocate reply buffer itself.

	 @param arg argument supplied from network packet
	 @param ret pointer to reply
	 @param retsize size of reply
	*/
	int (*co_handler)(void *arg, void **ret, int *retsize);
	/**
	 function to encode reply to store in reply cache
	 */
	c2_cache_encode_t	co_db_enc;
	/**
	 function to decode reply from reply cache
	 */
	c2_cache_decode_t	co_db_dec;
};

extern struct c2_compound_ops fops[C2_COMP_MAX_OP];

/**
 process incoming compound request
 */
int c2_compound_process(const struct c2_compound_op_arg *op,
			void *arg, void **ret, int *retsize);

#endif
