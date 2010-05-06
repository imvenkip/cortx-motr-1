/* -*- C -*- */
#ifndef _RPC_COMPOUND_SRV_

#define _RPC_COMPOUND_SRV_

#include "net/net_types.h"
#include "rpc/pcache.h"
#include "rpc/compound_types.h"

/**
 @section rpc-compound-server functions is related to server side
 */

/**
 handlers for operations incapsulated in compound request
 */
struct c2_compound_ops {
	c2_compound_op	co_op;
	c2_xdrproc_t	co_arg;
	c2_xdrproc_t	co_ret;
	int (*co_handler)(void *arg, void *ret);
	c2_pc_encode_t	co_db_enc;
	c2_pc_decode_t	co_db_dec;
};

int c2_compound_process(const struct c2_compound_op_arg *op,
			void *reply);
#endif
