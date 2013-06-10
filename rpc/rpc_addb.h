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
 * Original author: Rajanikant Chirmade <Rajanikant_Chirmade@xyratex.com>
 * Original creation date: 12/05/2012
 */
#pragma once

#ifndef __MERO_RPC_RPC_ADDB_H__
#define __MERO_RPC_RPC_ADDB_H__

#include "addb/addb.h"

/*
 * RPC ADDB Context types
 */
enum {
	M0_ADDB_CTXID_RPC_MOD     = 1000,
	M0_ADDB_CTXID_RPC_MACHINE,
	M0_ADDB_CTXID_RPC_FRM,
	M0_ADDB_CTXID_RPC_SERV
};

M0_ADDB_CT(m0_addb_ct_rpc_mod, M0_ADDB_CTXID_RPC_MOD);
M0_ADDB_CT(m0_addb_ct_rpc_machine, M0_ADDB_CTXID_RPC_MACHINE);
M0_ADDB_CT(m0_addb_ct_rpc_frm, M0_ADDB_CTXID_RPC_FRM);
M0_ADDB_CT(m0_addb_ct_rpc_serv, M0_ADDB_CTXID_RPC_SERV, "hi", "low");

/*
 * RPC ADDB Posting locations
 */
/** @todo Assign numbers to the constants */
enum {
	/* rpc service */
	M0_RPC_ADDB_LOC_SERVICE_ALLOC = 1,
	/* rpc_machine */
	M0_RPC_ADDB_LOC_MACHINE_NET_BUF_RECEIVED = 10,
	M0_RPC_ADDB_LOC_MACHINE_NET_BUF_ERR      = 20,
	M0_RPC_ADDB_LOC_MACHINE_RPC_TM_CLEANUP   = 30,
	M0_RPC_ADDB_LOC_MACHINE_RPC_CHAN_CREATE  = 40,
	/* rpc_formation */
	M0_RPC_ADDB_LOC_FORMATION_FRM_BALANCE = 1010,
	/* rpc_frmop */
	M0_RPC_ADDB_LOC_FRMOPS_PACKET_READY        = 2010,
	M0_RPC_ADDB_LOC_FRMOPS_NET_BUFFER_ALLOCATE = 2020,
	/* rpc_bulk */
	M0_RPC_ADDB_LOC_BULK_RPC_BULK_OP = 3010,
	M0_RPC_ADDB_LOC_BULK_BUF_INIT    = 3020,
	M0_RPC_ADDB_LOC_BULK_BUF_ADD     = 3030,
	M0_RPC_ADDB_LOC_BULK_BUF_OP      = 3040,
	/* rpc_session */
	M0_RPC_ADDB_LOC_SESSION_SLOT_TABLE_ALLOC_AND_INIT = 4010,
	M0_RPC_ADDB_LOC_SESSION_ESTABLISH                 = 4020,
	/* rpc_session_fop */
	M0_RPC_ADDB_LOC_SESSION_FOP_CONN_ESTABLISH_ITEM_DECODE = 5010,
	/* rpc_session_fom */
	M0_RPC_ADDB_LOC_SESSION_FOM_CONN_ESTABLISH_TICK    = 6010,
	M0_RPC_ADDB_LOC_SESSION_GEN_FOM_CREATE             = 6020,
	M0_RPC_ADDB_LOC_SESSION_FOM_SESSION_ESTABLISH_TICK = 6030,
	/* rpc_conn */
	M0_RPC_ADDB_LOC_CONN_SESSION_ZERO_ATTACH = 7010,
};

/*
 * RPC ADDB record identifiers.
 */
enum {
	M0_ADDB_RECID_RPC_STATS_ITEMS = 1000,
	M0_ADDB_RECID_RPC_STATS_PACKETS,
	M0_ADDB_RECID_RPC_STATS_BYTES,
	M0_ADDB_RECID_RPC_SENT_ITEM_SIZES,
	M0_ADDB_RECID_RPC_RCVD_ITEM_SIZES,
};
#undef KB
#define KB(d) (d) << 10
M0_ADDB_RT_CNTR(m0_addb_rt_rpc_sent_item_sizes,
		M0_ADDB_RECID_RPC_SENT_ITEM_SIZES,
		KB(1), KB(5), KB(10), KB(25), KB(50),
		KB(75), KB(100), KB(125), KB(150));
M0_ADDB_RT_CNTR(m0_addb_rt_rpc_rcvd_item_sizes,
		M0_ADDB_RECID_RPC_RCVD_ITEM_SIZES,
		KB(1), KB(5), KB(10), KB(25), KB(50),
		KB(75), KB(100), KB(125), KB(150));
#undef KB

/* Data point record type for rpc statistics. */
M0_ADDB_RT_DP(m0_addb_rt_rpc_stats_items, M0_ADDB_RECID_RPC_STATS_ITEMS,
	      "rcvd_items", "sent_items", "failed_items",
	      "dropped_items", "timeout_items");
M0_ADDB_RT_DP(m0_addb_rt_rpc_stats_packets, M0_ADDB_RECID_RPC_STATS_PACKETS,
	      "rcvd_packets", "sent_packets", "failed_packets");
M0_ADDB_RT_DP(m0_addb_rt_rpc_stats_bytes, M0_ADDB_RECID_RPC_STATS_BYTES,
	      "sent_bytes", "rcvd_bytes");

#endif /* __MERO_RPC_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
