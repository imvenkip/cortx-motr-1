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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 06/25/2011
 */

#ifndef C2_RPC_ONWIRE_H_
#define C2_RPC_ONWIRE_H_

/**
   @defgroup rpc_onwire RPC wire format types and interfaces

   A C2 RPC is a container for items, FOPs and ADDB records.  The wire
   format of an RPC consists of a header and a sequence of encoded
   RPC items.

   The header consists of, in order
    - version, a uint32_t, currently only C2_RPC_VERSION_1 is supported
    - the number of RPC items in the RPC

   Each item is encoded as
    - opcode value which denotes a unique operation code for an rpc item type &
    is encoded as a uint32_t
    - length number of bytes in the item payload, encoded as a uint32_t
    - Various fields related to internal processing of an rpc item.
    - item payload ( the serialized data for each item )

    Each RPC item is directly serialized/deserialized into a network buffer.
    using the various bufvec encode/decode functions for atomic types.
    (see xcode/bufvec_xcode.h).

    XXX : Currently we assume (and check) that the the c2_bufvec-s, supplied to
    transcode functions have 8-byte aligned buffers with sizes multiple of
    8 bytes.

    XXX : Current implementation of onwire formats is only for RPC items
	  which are containers for FOPs.
   @{
*/

#include "rpc/rpc2.h"
#include "xcode/bufvec_xcode.h"

enum {
	C2_RPC_VERSION_1 = 1,
};

/** Header information present in an RPC object */
struct c2_rpc_header {
	/** RPC version, currently 1 */
	uint32_t rh_ver;
	/** No of items present in the RPC object */
	uint32_t rh_item_count;
};

/**
   Header information per rpc item in an rpc object. The detailed description
   of the various fields is present in struct c2_rpc_item /rpc/rpc2.h.
*/
struct c2_rpc_item_header {
	uint32_t			rih_opcode;
	uint64_t			rih_item_size;
	uint64_t			rih_sender_id;
	uint64_t			rih_session_id;
	uint32_t			rih_slot_id;
	struct c2_rpc_sender_uuid	rih_uuid;
	struct c2_verno			rih_verno;
	struct c2_verno			rih_last_persistent_ver_no;
	struct c2_verno			rih_last_seen_ver_no;
	uint64_t			rih_xid;
	uint64_t			rih_slot_gen;
};

/**
   XXX: Temporary way to find the size of the onwire rpc object header
   and item header in bytes. The RPC_HEADER_FIELDS and ITEM_HEADER_FIELDS
   denote no of fields present in each onwire rpc object header and rpc item
   header respectively ( see struct c2_rpc_item and struct c2_rpc_item_header).
*/
enum {
	/** Count of fields in RPC object header (see struct c2_rpc_header) */
	RPC_HEADER_FIELDS = 2,
	/** Count of fields in item header (see struct c2_rpc_item_header) */
	ITEM_HEADER_FIELDS = 14,
	/** Onwire header size = RPC_HEADER_FIELDS * BYTES_PER_XCODE_UNIT */
	RPC_ONWIRE_HEADER_SIZE = 16,
	/** Onwire item header size = ITEM_HEADER_FIELDS * BYTES_PER_XCODE_UNIT */
	ITEM_ONWIRE_HEADER_SIZE = 112,
};

/**
   This function encodes an c2_rpc object into the supplied network buffer. Each
   rpc object contains a header and count of rpc items present in that rpc
   object. These items are serialized directly onto the bufvec present in the
   network buffer using the various bufvec encode/decode funtions for atomic
   types.
   The supplied network buffer is allocated and managed by the caller.
   @param rpc_obj  pointer to the rpc object which will be serialized
   into a network buffer
   @param nb pointer to network buffer on which the rpc object is to be
   serialized
   @retval 0 on  success.
   @retval -errno on failure.
*/
int c2_rpc_encode(struct c2_rpc *rpc_obj, struct c2_net_buffer *nb);

/**
   Decodes an onwire rpc object in the network buffer into an incore rpc object.
   The rpc object in the network buffer is deserialized using the bufvec
   encode/decode funtions to extract the header. The header contains the count
   of items present in the rpc object. For each rpc item we :-
   - Deserialize the opcode and lookup the  corresponding item_type.
   - Call the corresponding item decode function for that item type and
     deserialize the item payload.
   - Add the deserialized item to the r_items list of the rpc_obj.
   @param rpc_obj The RPC object on which the onwire items would be
   deserialized.
   @param nb     The network buffer containing the onwire RPC object.
   @param len    Length of RPC message received.
   @param offset offset of the message in the received buffer.
   @retval 0 on success.
   @retval -errno on failure.
*/
int c2_rpc_decode(struct c2_rpc *rpc_obj, struct c2_net_buffer *nb,
		  c2_bcount_t len, c2_bcount_t offset);

int item_encdec(struct c2_bufvec_cursor *cur, struct c2_rpc_item *item,
			enum c2_bufvec_what what);

/** @}  End of rpc_onwire group */

#endif
