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
 * Original author: Rohan Puri <Rohan_Puri@xyratex.com>
 * Original creation date: 12/04/2012
 * Revision       : Anup Barve <Anup_Barve@xyratex.com>
 * Revision date  : 05/02/2013
 */

#pragma once

#ifndef __MERO_SNS_SNS_ADDB_H__
#define __MERO_SNS_SNS_ADDB_H__

#include "addb/addb.h"

/**
   @addtogroup SNSRepairCM
   @{
 */

/*
 ******************************************************************************
 * SNS REPAIR Service ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_SNS_MOD = 400,
	M0_ADDB_CTXID_SNS_CM,
	M0_ADDB_CTXID_SNS_AG,
	M0_ADDB_CTXID_SNS_CP,
};

M0_ADDB_CT(m0_addb_ct_sns_mod, M0_ADDB_CTXID_SNS_MOD);
M0_ADDB_CT(m0_addb_ct_sns_cm, M0_ADDB_CTXID_SNS_CM, "hi", "low");
M0_ADDB_CT(m0_addb_ct_sns_ag, M0_ADDB_CTXID_SNS_AG);
M0_ADDB_CT(m0_addb_ct_sns_cp, M0_ADDB_CTXID_SNS_CP);


extern struct m0_addb_ctx m0_sns_mod_addb_ctx;
extern struct m0_addb_ctx m0_sns_cm_addb_ctx;
extern struct m0_addb_ctx m0_sns_ag_addb_ctx;
extern struct m0_addb_ctx m0_sns_cp_addb_ctx;

/*
 * ADDB record type identifiers for data points.
 * Do not change the numbering.
 */
enum {
	/**
	 * Records sizes of incoming and outgoing buffer pool,
	 * along with the current usage.
	 */
	M0_ADDB_RECID_SNS_CM_BUF_NR = 400,
	/**
	 * Records initialisation of an aggregation group.
	 * @ref m0_sns_cm_ag_alloc.
	 * This data point will include the aggregation group id.
	 */
	M0_ADDB_RECID_SNS_AG_ALLOC,
	/**
	 * Records finalisation of an aggregation group.
	 * @ref m0_sns_cm_ag_fini.
	 * This data point will include the aggregation group id.
	 */
	M0_ADDB_RECID_SNS_AG_FINI,
	/**
	 * Records the update of the local sliding window.
	 * @ref m0_cm_ag_advance.
	 * This data point will include the sliding window information.
	 */
	M0_ADDB_RECID_SNS_SW_UPDATE,
	/**
	 * Records the current gob fid that the iterator will start
	 * processing.
	 * @ref iter_fid_next.
	 */
	M0_ADDB_RECID_SNS_ITER_NEXT_GFID,
	/**
	 * Records the aggregation group information.
	 * i.e. 0) Aggregation group id.
	 *      1) Number of local copy packets.
	 *      2) Number of global copy packets.
	 *      3) Number of transformed copy packets.
	 *      4) Does this aggregation group have incoming copy packets.
	 *      5) Number of failure units.
	 */
	M0_ADDB_RECID_SNS_AG_INFO,
	/**
	 * Data point to record the copy packet information.
	 * viz. 0) Index of copy packet in aggregation group.
	 *      1) Aggregation group index to which this copy packet belongs.
	 *      2) Read/Write stob id.
	 *      3) Offset within the stob.
	 *      4) Local or outgoing copy packet status.
	 */
	M0_ADDB_RECID_SNS_CP_INFO,
	/**
	 * Data point to record sns repair information.
	 * viz. 0) Time taken to complete repair.
	 *      1) Size of repaired data. (This is total size of all files
	 *         repaired on the failed devices.
	 */
	M0_ADDB_RECID_SNS_REPAIR_INFO,
	/**
	 * Data point to record the progress of sns repair operation.
	 * Contains following fields,
	 * 0) Number of files scanned.
	 * 1) Number of aggregation groups scanned in the latest file being
	 *    repaired.
	 */
	M0_ADDB_RECID_SNS_REPAIR_PROGRESS,
};


/* Data point record types. */

M0_ADDB_RT_DP(m0_addb_rt_sns_cm_buf_nr, M0_ADDB_RECID_SNS_CM_BUF_NR,
	      "max_incoming_buf_nr", "max_outgoing_buf_nr",
	      "curr_incoming_buf_nr", "curr_outgoing_buf_nr");

M0_ADDB_RT_DP(m0_addb_rt_sns_ag_alloc, M0_ADDB_RECID_SNS_AG_ALLOC,
              "ai_hi:u_hi", "ai_hi:u_lo", "ai_lo:u_hi", "ai_lo:u_lo");

M0_ADDB_RT_DP(m0_addb_rt_sns_ag_fini, M0_ADDB_RECID_SNS_AG_FINI,
              "ai_hi:u_hi", "ai_hi:u_lo", "ai_lo:u_hi", "ai_lo:u_lo");

M0_ADDB_RT_DP(m0_addb_rt_sns_sw_update, M0_ADDB_RECID_SNS_SW_UPDATE,
              "sw_lo:ai_hi:u_hi", "sw_lo:ai_hi:u_lo",
	      "sw_lo:ai_lo:u_hi", "sw_lo:ai_lo:u_lo" ,
              "sw_hi:ai_hi:u_hi", "sw_hi:ai_hi:u_lo",
	      "sw_hi:ai_lo:u_hi", "sw_hi:ai_lo:u_lo");

M0_ADDB_RT_DP(m0_addb_rt_sns_iter_next_gfid, M0_ADDB_RECID_SNS_ITER_NEXT_GFID,
	      "gob_fid_container", "gob_fid_key");

M0_ADDB_RT_DP(m0_addb_rt_sns_ag_info, M0_ADDB_RECID_SNS_AG_INFO,
	      "ai_hi:u_hi", "ai_hi:u_lo", "ai_lo:u_hi", "ai_lo:u_lo",
	      "local_copy_packets_nr", "global_copy_packets_nr",
	      "transformed_copy_packers_nr", "has_incoming_copy_packets_flag",
	      "failure_units_nr");

M0_ADDB_RT_DP(m0_addb_rt_sns_cp_info, M0_ADDB_RECID_SNS_CP_INFO,
	     "ai_hi:u_hi", "ai_hi:u_lo", "ai_lo:u_hi",
	     "ai_lo:u_lo", "stob_id:u_hi", "stob_id:u_lo", "offset_in_stob",
	     "is_local_status");

M0_ADDB_RT_DP(m0_addb_rt_sns_repair_info, M0_ADDB_RECID_SNS_REPAIR_INFO,
	      "time in nanosecs", "size in bytes");

M0_ADDB_RT_DP(m0_addb_rt_sns_repair_progress, M0_ADDB_RECID_SNS_REPAIR_PROGRESS,
	      "files_scanned_nr", "ags_scanned_in_latest_file_being_repaired",
	      "total_ags_in_latest_file_being_repaired");

/**
 * SNS function failure macro using the global ADDB machine to post.
 * @param rc Return code.
 * @param loc Location code - one of the SNS_ADDB_LOC_ enumeration constants.
 * @param ctx Runtime context pointer.
 * @pre rc < 0.
 */
#define SNS_ADDB_FUNCFAIL(rc, ctx, loc)                              \
M0_ADDB_FUNC_FAIL(&m0_addb_gmc, M0_SNS_ADDB_LOC_##loc, rc, ctx)

#define SNS_ALLOC_PTR(ptr, ctx, loc)                                 \
M0_ALLOC_PTR_ADDB(ptr, &m0_addb_gmc, M0_SNS_ADDB_LOC_##loc, ctx)

#define SNS_ALLOC_ARR(ptr, nr, ctx, loc)                             \
M0_ALLOC_ARR_ADDB(ptr, nr, &m0_addb_gmc, M0_SNS_ADDB_LOC_##loc, ctx)

/*
 ******************************************************************************
 * SNS ADDB posting locations
 ******************************************************************************
 */
enum {
	M0_SNS_ADDB_LOC_CP_STORAGE_OV_VEC,
	M0_SNS_ADDB_LOC_CP_STORAGE_OV_BUF,
	M0_SNS_ADDB_LOC_CP_IO,
	M0_SNS_ADDB_LOC_CP_STIO,
	M0_SNS_ADDB_LOC_CP_XFORM_BUFVEC,
	M0_SNS_ADDB_LOC_CP_XFORM_BUFVEC_MERGE,
	M0_SNS_ADDB_LOC_CP_XFORM_BUFVEC_SPLIT,
	M0_SNS_ADDB_LOC_CP_SEND,
	M0_SNS_ADDB_LOC_CP_SEND_FOP,
	M0_SNS_ADDB_LOC_CP_RECV_INIT,
	M0_SNS_ADDB_LOC_CP_RECV_WAIT,
	M0_SNS_ADDB_LOC_CP_INDEXVEC_PREPARE,
	M0_SNS_ADDB_LOC_CP_TO_CPX_IVEC,
	M0_SNS_ADDB_LOC_CP_TO_CPX_DESC,
	M0_SNS_ADDB_LOC_AG_ALLOC,
	M0_SNS_ADDB_LOC_AG_FAIL_CTX,
	M0_SNS_ADDB_LOC_CP_ALLOC,
	M0_SNS_ADDB_LOC_ITER_FID_NEXT,
	M0_SNS_ADDB_LOC_ITER_CDOM_GET,
};

/** @} */ /* end of SNSRepairCM group */

#endif /* __MERO_SNS_REPAIR_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

