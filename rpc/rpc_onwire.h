#ifndef C2_RPC_ONWIRE_H_
#define C2_XDR_ONWIRE_H_

/**
   @defgroup rpc_onwire RPC wire format types and interfaces

   A C2 RPC is a container for items, FOPs and ADDB records.  The wire
   encoding of an RPC consists of a header and a sequence of encoded
   RPC items.

   The header consists of, in order
    - version, a uint32_t, currently only C2_RPC_VERSION_0 is supported
    - the number of RPC items in the RPC

   Each item is encoded as
    - opcode value present in the fop ( ft_code ) encoded as a uint32_t
    - length number of bytes in the item payload, encoded as a uint32_t
    - Various fields related to internal processing of an rpc item.
    - item payload ( the serialized fop data )

    XXX : Current implementation of onwire formats is only for RPC items
	  which are containers for FOPs.
   @{
*/

#include "rpc/rpccore.h"
#include <rpc/xdr.h>

/** Header information present in an RPC object */
struct c2_rpc_header {
	/** RPC version, currenly 1 */
	uint32_t rh_ver;
	/** No of items present in the RPC object */
	uint32_t item_count;
};

/** Header information per rpc item in an rpc object. The detailed
    description of the various fields is present in struct c2_rpc_item
    /rpc/rpccore.h */
struct c2_rpc_item_header {
	uint64_t			rih_length;
	uint64_t			rih_sender_id;
	uint64_t			rih_session_id;
	uint32_t			slot_id;
	struct c2_rpc_sender_uuid	rih_uuid;
	struct c2_verno			rih_verno;
	struct c2_verno			rih_last_persistent_ver_no;
	struct c2_verno			rih_last_seen_ver_no;
	uint64_t			rih_xid;
	uint64_t			rih_slot_gen;
};

/**
   This function encodes c2_rpc object into the specified buffer. Each
   rpc object contains a header and number of rpc items. These items are
   serialized using an XDR stream created by xdrmem_create() or equivalent.
   The specified buffer is allocated and managed by the caller.
   @param rpc_obj - rpc object to be converted into a network buffer
   @param nb - network buffer
   @retval - 0 if success, errno on failure.
*/
int c2_rpc_encode(struct c2_rpc *rpc_obj, struct c2_net_buffer *nb);

/**
   Decodes the data in the network buffer into a newly allocated buffer.
   The data in the buffer is deserialized using XDR to extract the header
   and various RPC items.The opcodes present in the individual rpc items
   look up the decode operation to allocate the new RPC item and to decode
   the opaque payload of the item
   @param nb - The network buffer containing the onwire RPC object.
   @retval - 0 on success, errno on failure;
*/
int c2_rpc_decode( struct c2_rpc *rpc_obj, struct c2_net_buffer *nb );

/**
   Generic serialization routine for a rpc item,
   These will eventually be added  to c2_rpc_item_type_ops
   @param xdrs - XDR stream used for serialization
   @param item - item to be serialized
   @retval rc - 0 if success, errno if failure
*/
int c2_rpc_fop_default_encode(struct c2_rpc_item_type *item_type,
			      struct c2_rpc_item *item,
			      struct c2_bufvec_cursor *cur);

/**
   Generic deserialization routine for a rpc item,
   These will eventually be added  to c2_rpc_item_type_ops
   @param xdrs - XDR stream used for serialization
   @param item - item to be deserialized
   @retval rc - 0 if success, errno if failure
*/
int c2_rpc_fop_default_decode(struct c2_rpc_item_type *item_type,
			      struct c2_rpc_item **item,
			      struct c2_bufvec_cursor *cur);

/** Return the onwire size of the item in bytes
    The onwire size equals = size of (header + payload)
    @param item - The rpc item for which the on wire
		   size is to be calculated
    @retval	- size of the item in bytes.
*/
size_t c2_rpc_item_default_size(struct c2_rpc_item *item);

/** @}  End of rpc_onwire group */

#endif
