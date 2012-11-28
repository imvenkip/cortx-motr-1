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

#ifndef __MERO_RPC_ONWIRE_H__
#define __MERO_RPC_ONWIRE_H__

#include "lib/types.h" /* uint64_t */
#include "dtm/verno.h" /* m0_verno */
#include "dtm/verno_xc.h" /* m0_verno_xc */
#include "xcode/xcode_attr.h" /* M0_XCA_RECORD */

/**
 * @addtogroup rpc
 * @{
 */

enum {
	M0_RPC_VERSION_1 = 1,
};

/**
   Requirements:
   * UUID must change whenever a storage-less client re-boots.
   * for a client with persistent state (e.g., a disk) uuid
     must survive reboots.
*/
struct m0_rpc_sender_uuid {
	/** XXX Temporary */
	uint64_t su_uuid;
} M0_XCA_RECORD;

struct m0_rpc_onwire_slot_ref {

	struct m0_rpc_sender_uuid  osr_uuid;

	uint64_t                   osr_sender_id;

	uint64_t                   osr_session_id;

	/** Numeric id of slot. Used when encoding and decoding rpc item to
	    and from wire-format
	 */
	uint32_t                   osr_slot_id;

	/** If slot has verno matching sr_verno, then only the item can be
	    APPLIED to the slot
	 */
	struct m0_verno            osr_verno;
	/**
	 * @todo These are temporary fields; there is no need to duplicate
	 * this information with each and every reply. In the future, special
	 * 1-way item will be used to transfer this information.
	 */
	/** In each reply item, receiver reports to sender, verno of item
	    whose effects have reached persistent storage, using this field
	 */
	struct m0_verno            osr_last_persistent_verno;

	/** Inform the sender about current slot version */
	struct m0_verno            osr_last_seen_verno;

	/** An identifier that uniquely identifies item within
	    slot->item_list.
        */
	uint64_t                   osr_xid;

	/** Generation number of slot */
	uint64_t                   osr_slot_gen;
} M0_XCA_RECORD;

struct m0_rpc_packet_onwire_header {
	/* Version */
	uint32_t poh_version;

	/** Number of RPC items in packet */
	uint32_t poh_nr_items;

	uint64_t poh_magic;
} M0_XCA_RECORD;

struct m0_rpc_item_onwire_header {
	/* Item opcode */
	uint32_t ioh_opcode;

	uint64_t ioh_magic;
} M0_XCA_RECORD;

M0_INTERNAL int m0_rpc_item_header_encode(struct m0_rpc_item_onwire_header *ioh,
					  struct m0_bufvec_cursor *cur);

/**
    Decodes the rpc item header into a bufvec
    @param ioh RPC item header to be decoded
    @param cur Current bufvec cursor position
    @retval 0 (success)
    @retval -errno  (failure)
*/
M0_INTERNAL int m0_rpc_item_header_decode(struct m0_bufvec_cursor *cur,
					  struct m0_rpc_item_onwire_header
					  *ioh);

/** @}  End of rpc group */

#endif /* __MERO_RPC_ONWIRE_H__ */
