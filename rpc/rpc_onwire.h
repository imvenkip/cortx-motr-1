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

#ifndef __COLIBRI_RPC_ONWIRE_H__
#define __COLIBRI_RPC_ONWIRE_H__

#include "lib/types.h" /* uint64_t */
#include "dtm/verno.h" /* c2_verno */
#include "dtm/verno_xc.h" /* c2_verno_xc */
#include "xcode/xcode_attr.h" /* C2_XCA_RECORD */

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

/**
   Requirements:
   * UUID must change whenever a storage-less client re-boots.
   * for a client with persistent state (e.g., a disk) uuid
     must survive reboots.
*/
struct c2_rpc_sender_uuid {
	/** XXX Temporary */
	uint64_t su_uuid;
} C2_XCA_RECORD;

struct c2_rpc_onwire_slot_ref {

	struct c2_rpc_sender_uuid  osr_uuid;

	uint64_t                   osr_sender_id;

	uint64_t                   osr_session_id;

	/** Numeric id of slot. Used when encoding and decoding rpc item to
	    and from wire-format
	 */
	uint32_t                   osr_slot_id;

	/** If slot has verno matching sr_verno, then only the item can be
	    APPLIED to the slot
	 */
	struct c2_verno            osr_verno;

	/** In each reply item, receiver reports to sender, verno of item
	    whose effects have reached persistent storage, using this field
	 */
	struct c2_verno            osr_last_persistent_verno;

	/** Inform the sender about current slot version */
	struct c2_verno            osr_last_seen_verno;

	/** An identifier that uniquely identifies item within
	    slot->item_list.
        */
	uint64_t                   osr_xid;

	/** Generation number of slot */
	uint64_t                   osr_slot_gen;
} C2_XCA_RECORD;

struct c2_rpc_packet_onwire_header {
	/* Version */
	uint32_t poh_version;
	/** Number of RPC items in packet */
	uint32_t poh_nr_items;
} C2_XCA_RECORD;

struct c2_rpc_item_onwire_header {
	/* Item opcode */
	uint32_t                      ioh_opcode;
	/* Item length */
	uint64_t                      ioh_length;
	/* Onwire slot reference */
	struct c2_rpc_onwire_slot_ref ioh_slot_ref;
} C2_XCA_RECORD;

/** @}  End of rpc_onwire group */

#endif /* __COLIBRI_RPC_ONWIRE_H__ */
