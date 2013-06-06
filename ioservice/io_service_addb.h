/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 11/30/2012
 */

#pragma once

#ifndef __MERO_IOSERVICE_IO_SERVICE_ADDB_H__
#define __MERO_IOSERVICE_IO_SERVICE_ADDB_H__

#include "addb/addb.h"

/**
   @addtogroup reqh
   @{
 */

/*
 ******************************************************************************
 * IO Service ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_IOS_MOD  = 100,
	M0_ADDB_CTXID_IOS_SERV,
	M0_ADDB_CTXID_COB_CREATE_FOM,
	M0_ADDB_CTXID_COB_DELETE_FOM,
	M0_ADDB_CTXID_COB_IO_RW_FOM
};

M0_ADDB_CT(m0_addb_ct_ios_mod, M0_ADDB_CTXID_IOS_MOD);

#ifndef __KERNEL__
M0_ADDB_CT(m0_addb_ct_ios_serv, M0_ADDB_CTXID_IOS_SERV, "hi", "low");
#endif

/*
 ******************************************************************************
 * IO Service ADDB record type identifiers.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	/** See struct m0_ios_rwfom_stats::ifs_sizes_cntr */
	M0_ADDB_RECID_IOS_RFOM_SIZES = 100,
	/** See struct m0_ios_rwfom_stats::ifs_sizes_cntr */
	M0_ADDB_RECID_IOS_WFOM_SIZES,
	/** See struct m0_ios_rwfom_stats::ifs_times_cntr */
	M0_ADDB_RECID_IOS_RFOM_TIMES,
	/** See struct m0_ios_rwfom_stats::ifs_times_cntr */
	M0_ADDB_RECID_IOS_WFOM_TIMES,
	/** Data point to record I/O FOM finish */
	M0_ADDB_RECID_IOS_RWFOM_FINISH,
	/** Data point to record COB-create FOM finish */
	M0_ADDB_RECID_IOS_CCFOM_FINISH,
	/** Data point to record COB-delete FOM finish */
	M0_ADDB_RECID_IOS_CDFOM_FINISH,
	/** Data point record to post throughput of an IO request */
	M0_ADDB_RECID_IOS_IO_FINISH,
	/** Data point record to post throughput of an individual
	 * descriptor in IO request */
	M0_ADDB_RECID_IOS_DESC_IO_FINISH,
	/** Data point record to convey buffer pool low condition */
	M0_ADDB_RECID_IOS_BUFFER_POOL_LOW,
};

/** @todo adjust IOS counter histogram buckets */
#undef KB
#define KB(d) (d) << 10
M0_ADDB_RT_CNTR(m0_addb_rt_ios_rfom_sizes,  M0_ADDB_RECID_IOS_RFOM_SIZES,
		KB(4), KB(16), KB(32), KB(64), KB(128), KB(256), KB(512),
		KB(768));
M0_ADDB_RT_CNTR(m0_addb_rt_ios_wfom_sizes,  M0_ADDB_RECID_IOS_WFOM_SIZES,
		KB(4), KB(16), KB(32), KB(64), KB(128), KB(256), KB(512),
		KB(768));
#undef KB

M0_ADDB_RT_CNTR(m0_addb_rt_ios_rfom_times,  M0_ADDB_RECID_IOS_RFOM_TIMES,
		100, 200, 300, 400, 500, 600, 700, 800, 900);

M0_ADDB_RT_CNTR(m0_addb_rt_ios_wfom_times,  M0_ADDB_RECID_IOS_WFOM_TIMES,
		100, 200, 300, 400, 500, 600, 700, 800, 900);

/* Data point record type for io service */
M0_ADDB_RT_DP(m0_addb_rt_ios_rwfom_finish,
	      M0_ADDB_RECID_IOS_RWFOM_FINISH,
	      "return_code", "io_size", "turnaround_time_ns");
M0_ADDB_RT_DP(m0_addb_rt_ios_ccfom_finish,
	      M0_ADDB_RECID_IOS_CCFOM_FINISH,
	      "stob_id.hi", "stob_id.lo", "rc");
M0_ADDB_RT_DP(m0_addb_rt_ios_cdfom_finish,
	      M0_ADDB_RECID_IOS_CDFOM_FINISH,
	      "stob_id.hi", "stob_id.lo", "rc");

/*
 ******************************************************************************
 * Utility macros
 ******************************************************************************
 */

/**
 */
#ifndef __KERNEL__
M0_ADDB_CT(m0_addb_ct_cob_create_fom, M0_ADDB_CTXID_COB_CREATE_FOM,
	   "gob_fid_container", "gob_fid_key", "cob_fid_container",
	   "cob_fid_key");

M0_ADDB_CT(m0_addb_ct_cob_delete_fom, M0_ADDB_CTXID_COB_DELETE_FOM,
	   "gob_fid_container", "gob_fid_key", "cob_fid_container",
	   "cob_fid_key");

M0_ADDB_CT(m0_addb_ct_cob_io_rw_fom, M0_ADDB_CTXID_COB_IO_RW_FOM,
	   "file_fid_container", "file_fid_key", "nb_descr_nr",
	   "flags");
#endif

/**
   IOS function failure macro using the global ADDB machine to post.
   @param rc Return code
   @param loc Location code - one of the IOS_ADDB_LOC_ enumeration constants
   @param ctx Runtime context pointer
   @pre rc < 0
 */
#define IOS_ADDB_FUNCFAIL(rc, loc, ctx)		                     \
M0_ADDB_FUNC_FAIL(&m0_addb_gmc, M0_IOS_ADDB_LOC_##loc, rc, ctx)

#define IOS_ALLOC_PTR(ptr, ctx, loc)			             \
M0_ALLOC_PTR_ADDB(ptr, &m0_addb_gmc, M0_IOS_ADDB_LOC_##loc, ctx)

#define IOS_ALLOC_ARR(ptr, nr, ctx, loc)                             \
M0_ALLOC_ARR_ADDB(ptr, nr, &m0_addb_gmc, M0_IOS_ADDB_LOC_##loc, ctx)

/*
 ******************************************************************************
 * Request handler ADDB posting locations
 ******************************************************************************
 */
/** @todo Assign numbers to locations */
enum {
	M0_IOS_ADDB_LOC_CREATE_BUF_POOL,
	M0_IOS_ADDB_LOC_SERVICE_ALLOC,
	M0_IOS_ADDB_LOC_FOM_COB_RW_CREATE,
	M0_IOS_ADDB_LOC_ZERO_COPY_INITIATE_1,
	M0_IOS_ADDB_LOC_ZERO_COPY_INITIATE_2,
	M0_IOS_ADDB_LOC_ZERO_COPY_FINISH,
	M0_IOS_ADDB_LOC_IO_LAUNCH_1,
	M0_IOS_ADDB_LOC_IO_LAUNCH_2,
	M0_IOS_ADDB_LOC_IO_LAUNCH_3,
	M0_IOS_ADDB_LOC_IO_LAUNCH_4,
	M0_IOS_ADDB_LOC_IO_FINISH,
	M0_IOS_ADDB_LOC_INDEXVEC_WIRE2MEM_1,
	M0_IOS_ADDB_LOC_INDEXVEC_WIRE2MEM_2,
	M0_IOS_ADDB_LOC_ALIGN_BUFVEC_1,
	M0_IOS_ADDB_LOC_ALIGN_BUFVEC_2,
	M0_IOS_ADDB_LOC_COB_FOM_CREATE_1,
	M0_IOS_ADDB_LOC_COB_FOM_CREATE_2,
	M0_IOS_ADDB_LOC_CC_STOB_CREATE_1,
	M0_IOS_ADDB_LOC_CC_STOB_CREATE_2,
	M0_IOS_ADDB_LOC_CC_COB_CREATE_1,
	M0_IOS_ADDB_LOC_CC_COB_CREATE_2,
	M0_IOS_ADDB_LOC_CD_COB_DELETE_1,
	M0_IOS_ADDB_LOC_CD_COB_DELETE_2,
	M0_IOS_ADDB_LOC_CD_STOB_DELETE_1,
	M0_IOS_ADDB_LOC_CD_STOB_DELETE_2,
	/** io_fops.c */
	M0_IOS_ADDB_LOC_IO_FOP_INIT,
	M0_IOS_ADDB_LOC_IO_FOP_DESC_IVEC_PREP,
	M0_IOS_ADDB_LOC_IO_FOP_COALESCE_1,
	M0_IOS_ADDB_LOC_IO_FOP_COALESCE_2,
	M0_IOS_ADDB_LOC_IO_FOP_COALESCE_3,
	M0_IOS_ADDB_LOC_ITEM_IO_COALESCE,
	M0_IOS_ADDB_LOC_IO_ITEM_REPLIED,
	M0_IOS_ADDB_LOC_IO_FOP_SEG_INIT,
	M0_IOS_ADDB_LOC_IO_FOP_IVEC_ALLOC_1,
	M0_IOS_ADDB_LOC_IO_FOP_IVEC_ALLOC_2,
	M0_IOS_ADDB_LOC_IO_FOP_DESC_ALLOC,
};

extern struct m0_addb_ctx m0_ios_addb_ctx;

/* Total time required and size for IO */
M0_ADDB_RT_DP(m0_addb_rt_ios_io_finish, M0_ADDB_RECID_IOS_IO_FINISH,
	      "io_size" /* in bytes */, "time" /* in nsec */);

/* Time required and io size for each individual descriptors */
M0_ADDB_RT_DP(m0_addb_rt_ios_desc_io_finish, M0_ADDB_RECID_IOS_DESC_IO_FINISH,
	      "offset", "io_size" /* in bytes */, "time" /* in nsec */);

/* Buffer pool is low */
M0_ADDB_RT_DP(m0_addb_rt_ios_buffer_pool_low,
	      M0_ADDB_RECID_IOS_BUFFER_POOL_LOW);

/** @} */ /* end of IOS group */

#endif /* __MERO_IOSERVICE_IO_SERVICE_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
