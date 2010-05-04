/* -*- C -*- */
#ifndef _RPC_COMPOUND_

#define _RPC_COMPOUND_
/**
 @page rpc-compound COMPOUND related functions

 COMPOUND is ability to send many operations with own parameters
 inside single RPC command.
 this will reduce latencety to process single rpc

 @ref rpc-compound-types
 */

/**
 send COMPOUND rpc without update seqence, and not uses a slot.
 function allocate request, put operations in own body and quered
 to send.

 @param cli - connected rpc client
 @param num_ops - number of operations (not used now)
 @param ops - pointer to array of operation (not used now)

 @retval 0 - RPC is quered to send.
 @retval <0 - RPC not quered to send due errors
 */
int c2_compound_send_noseq(const struct rpc_client *cli,
			   const unsigned int num_ops, const void *ops);

/**
 send COMPOUND rpc with uses slot and update sequence.

 in additionaly to c2_compound_send_noseq function - that functions
 create sequence operation and insert as first operation in RPC.
 others operations is added after sequence operation.

 @param cli - connected rpc client
 @param slot - session slot used to send message.
 @param num_ops - number of operations (not used now)
 @param ops - pointer to array of operation (not used now)

 @retval 0 - RPC is quered to send.
 @retval <0 - RPC not quered to send due errors
 */
int c2_compound_send_seq(const struct c2_rpc_client *cli, struct c2_cli_slot *slot,
			 const unsigned int num_ops, const void *ops);

#endif
