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

#include "lib/types.h"        /* uint64_t */
#include "lib/types_xc.h"     /* m0_uint128_xc */
#include "lib/cookie.h"
#include "lib/cookie_xc.h"
#include "xcode/xcode_attr.h" /* M0_XCA_RECORD */

/**
 * @addtogroup rpc
 * @{
 */

enum {
	M0_RPC_VERSION_1 = 1,
};

struct m0_rpc_packet_onwire_header {
	/* Version */
	uint32_t poh_version;
	/** Number of RPC items in packet */
	uint32_t poh_nr_items;
	uint64_t poh_magic;
} M0_XCA_RECORD;

struct m0_rpc_item_header1 {
	/** Item opcode */
	uint32_t ioh_opcode;
	/** Item flags, taken from enum m0_rpc_item_flags. */
	uint32_t ioh_flags;
	/** HA epoch transferred by the item. */
	uint64_t ioh_ha_epoch;
	uint64_t ioh_magic;
} M0_XCA_RECORD;

struct m0_rpc_item_header2 {
	struct m0_uint128 osr_uuid;
	uint64_t          osr_sender_id;
	uint64_t          osr_session_id;
	uint64_t          osr_xid;
	struct m0_cookie  osr_cookie;
} M0_XCA_RECORD;

M0_INTERNAL int m0_rpc_item_header1_encdec(struct m0_rpc_item_header1 *ioh,
					   struct m0_bufvec_cursor *cur,
					   enum m0_xcode_what what);

/** @}  End of rpc group */

#endif /* __MERO_RPC_ONWIRE_H__ */
