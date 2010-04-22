/* -*- C -*- */
#ifndef _RPC_COMPOUND_

#define _RPC_COMPOUND_

/**
 send COMPOUND rpc without touch seqence, and not uses a slot.
 function is allocate request and put operations in own body.

 @param cli - connected rpc client
 @param num_ops - number of operations (not used now)
 @param ops - pointer to array of operation (not used now)

 @retval 0 - RPC is quered to send.
 @retval <0 - RPC not quered to send due errors
 */
int compound_send_noseq(struct rpc_client const *cli,
		        unsigned int num_ops, void *ops);

/**
 send COMPOUND rpc with uses slot and update sequence
 function is create sequence operation and insert as first operation in message.
 others operations is added after sequence operations.

 @param cli - connected rpc client
 @param slot - session slot used to send message.
 @param num_ops - number of operations (not used now)
 @param ops - pointer to array of operation (not used now)

 @retval 0 - RPC is quered to send.
 @retval <0 - RPC not quered to send due errors
 */
int compound_send_seq(struct c2_rpc_client const *cli, struct c2_cli_slot *slot,
		      unsigned int const num_ops, void const *ops);

#endif
