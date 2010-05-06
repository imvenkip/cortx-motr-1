/* -*- C -*- */
#ifndef _RPC_COMPOUND_SRV_

#define _RPC_COMPOUND_SRV_

#include "net/net_types.h"
#include "lib/cache.h"
#include "rpc/compound_types.h"

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

int c2_compound_process(const struct c2_compound_op_arg *op,
			void *arg, void **ret, int *retsize);

#endif
