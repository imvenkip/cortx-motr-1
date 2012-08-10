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

#pragma once

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

#include "xcode/bufvec_xcode.h"   /* enum c2_bufvec_what */

enum {
	C2_RPC_VERSION_1 = 1,
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

int c2_rpc_item_header_encdec(struct c2_rpc_item      *item,
			      struct c2_bufvec_cursor *cur,
			      enum c2_bufvec_what      what);

/** @}  End of rpc_onwire group */

#endif
