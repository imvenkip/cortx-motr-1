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
 * Original author: Andriy Tkachuk <andriy_tkachuk@xyratex.com>
 * Original creation date: 04/17/2013
 */

#pragma once

#ifndef __MERO_M0T1FS_M0T1FS_ADDB_H__
#define __MERO_M0T1FS_M0T1FS_ADDB_H__

#include "addb/addb.h"
#include "addb/addb_monitor.h"
#ifdef __KERNEL__
#include "fop/fom_simple.h"
#endif

/*
 ******************************************************************************
 * Kernel client ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_M0T1FS_MOD	= 500,
	M0_ADDB_CTXID_M0T1FS_MOUNTP	= 501,
	M0_ADDB_CTXID_M0T1FS_OP_READ	= 502,
	M0_ADDB_CTXID_M0T1FS_OP_WRITE	= 503,
};

/**
 * @note: After adding addb ctx/rec types for kernel, we need to
 * add their registration in addb_register_kernel_ctx_and_rec_types()
 * in file addb.c (for user-space), so that users of ADDB retrieval
 * interfaces can recognize them.
 * In addition, the READ and WRITE records are checked in the m0t1fs_rsink ST.
 * Changes to these context names also require changes in m0t1fs_rsink ST.
 */
M0_ADDB_CT(m0_addb_ct_m0t1fs_mod,	M0_ADDB_CTXID_M0T1FS_MOD);
M0_ADDB_CT(m0_addb_ct_m0t1fs_mountp,	M0_ADDB_CTXID_M0T1FS_MOUNTP);
M0_ADDB_CT(m0_addb_ct_m0t1fs_op_read,	M0_ADDB_CTXID_M0T1FS_OP_READ);
M0_ADDB_CT(m0_addb_ct_m0t1fs_op_write,	M0_ADDB_CTXID_M0T1FS_OP_WRITE);

extern struct m0_addb_ctx m0t1fs_addb_ctx;

enum {
	M0T1FS_ADDB_LOC_AIO_READ		= 10,
	M0T1FS_ADDB_LOC_AIO_WRITE		= 20,
	M0T1FS_ADDB_LOC_AIO_REQ			= 30,
	M0T1FS_ADDB_LOC_DBUF_ALLOI_BUF		= 40,
	M0T1FS_ADDB_LOC_IOMAPS_PREP_GRPARR	= 50,
	M0T1FS_ADDB_LOC_IOMAPS_PREP_MAP		= 60,
	M0T1FS_ADDB_LOC_IOMAPS_PREP_MAPS	= 70,
	M0T1FS_ADDB_LOC_IOMAP_INIT_DBUFS_COL	= 80,
	M0T1FS_ADDB_LOC_IOMAP_INIT_DBUFS_ROW	= 90,
	M0T1FS_ADDB_LOC_IOMAP_INIT_IV		= 100,
	M0T1FS_ADDB_LOC_IOMAP_INIT_PBUFS_COL	= 110,
	M0T1FS_ADDB_LOC_IOMAP_INIT_PBUFS_ROW	= 120,
	M0T1FS_ADDB_LOC_IOREQ_INIT_BVECB	= 130,
	M0T1FS_ADDB_LOC_IOREQ_INIT_BVECC	= 140,
	M0T1FS_ADDB_LOC_IOREQ_INIT_IV		= 150,
	M0T1FS_ADDB_LOC_IOREQ_INIT_PGATTRS	= 160,
	M0T1FS_ADDB_LOC_IVEC_CREAT_IV		= 170,
	M0T1FS_ADDB_LOC_PARITY_RECALC_DBUFS	= 180,
	M0T1FS_ADDB_LOC_PARITY_RECALC_PBUFS	= 190,
	M0T1FS_ADDB_LOC_PARITY_RECALC_OLD_BUFS	= 200,
	M0T1FS_ADDB_LOC_TIOREQ_GET_TI		= 210,
	M0T1FS_ADDB_LOC_TIOREQ_MAP_QDEVST	= 215,
	M0T1FS_ADDB_LOC_TIOREQ_MAP_QSPSLOT	= 217,
	M0T1FS_ADDB_LOC_TI_FOP_PREP		= 220,
	M0T1FS_ADDB_LOC_TI_REQ_INIT_IV		= 230,
	M0T1FS_ADDB_LOC_DGMODE_PROCESS_1        = 240,
	M0T1FS_ADDB_LOC_DGMODE_PROCESS_2        = 250,
	M0T1FS_ADDB_LOC_DGMODE_RECOV_DATA       = 260,
	M0T1FS_ADDB_LOC_DGMODE_RECOV_PARITY     = 270,
	M0T1FS_ADDB_LOC_DGMODE_RECOV_FAILVEC    = 280,
	M0T1FS_ADDB_LOC_READVEC_ALLOC_INIT      = 290,
	M0T1FS_ADDB_LOC_READVEC_ALLOC_BVEC      = 300,
	M0T1FS_ADDB_LOC_READVEC_ALLOC_BVEC_CNT  = 310,
	M0T1FS_ADDB_LOC_READVEC_ALLOC_PAGEATTR  = 320,
	M0T1FS_ADDB_LOC_READVEC_ALLOC_IVEC_FAIL = 330,

	M0T1FS_ADDB_RECID_IO_FINISH             = 500,
	M0T1FS_ADDB_RECID_COB_IO_FINISH         = 501,
	M0T1FS_ADDB_RECID_ROOT_COB              = 502,
	M0T1FS_ADDB_RECID_IOR_SIZES             = 503,
	M0T1FS_ADDB_RECID_IOW_SIZES             = 504,
	M0T1FS_ADDB_RECID_IOR_TIMES             = 505,
	M0T1FS_ADDB_RECID_IOW_TIMES             = 506,
	M0T1FS_ADDB_RECID_DGIOR_SIZES           = 507,
	M0T1FS_ADDB_RECID_DGIOW_SIZES           = 508,
	M0T1FS_ADDB_RECID_DGIOR_TIMES           = 509,
	M0T1FS_ADDB_RECID_DGIOW_TIMES           = 510,
	M0T1FS_ADDB_RECID_MON_IO_SIZE           = 511,
};

/* Total time required and size for IO */
M0_ADDB_RT_DP(m0_addb_rt_m0t1fs_io_finish, M0T1FS_ADDB_RECID_IO_FINISH,
	      "rw", "io_size" /* in bytes */, "time_ns");

/* Time required and io size for each COB FID */
M0_ADDB_RT_DP(m0_addb_rt_m0t1fs_cob_io_finish, M0T1FS_ADDB_RECID_COB_IO_FINISH,
	      "cob_container", "cob_key",
	      "io_size" /* in bytes (data + parity) */, "time_ns");

/* Number of bytes read, written from this client instance (m0t1fs) */
M0_ADDB_RT_STATS(m0_addb_rt_m0t1fs_mon_io_size, M0T1FS_ADDB_RECID_MON_IO_SIZE,
	      "total_rio_size", "total_wio_size");

/* m0t1fs root cob */
M0_ADDB_RT_DP(m0_addb_rt_m0t1fs_root_cob, M0T1FS_ADDB_RECID_ROOT_COB,
	      "cob_container", "cob_key");

#undef KB
#define KB(d) (d) << 10
M0_ADDB_RT_CNTR(m0_addb_rt_m0t1fs_ior_sizes,  M0T1FS_ADDB_RECID_IOR_SIZES,
		KB(4), KB(16), KB(32), KB(64), KB(128), KB(256), KB(512),
		KB(768));
M0_ADDB_RT_CNTR(m0_addb_rt_m0t1fs_iow_sizes,  M0T1FS_ADDB_RECID_IOW_SIZES,
		KB(4), KB(16), KB(32), KB(64), KB(128), KB(256), KB(512),
		KB(768));
M0_ADDB_RT_CNTR(m0_addb_rt_m0t1fs_dgior_sizes,  M0T1FS_ADDB_RECID_DGIOR_SIZES,
		KB(4), KB(16), KB(32), KB(64), KB(128), KB(256), KB(512),
		KB(768));
M0_ADDB_RT_CNTR(m0_addb_rt_m0t1fs_dgiow_sizes,  M0T1FS_ADDB_RECID_DGIOW_SIZES,
		KB(4), KB(16), KB(32), KB(64), KB(128), KB(256), KB(512),
		KB(768));
#undef KB

#undef uS
#undef mS
#define uS(us) us
#define mS(ms) uS(1000 * (ms))
M0_ADDB_RT_CNTR(m0_addb_rt_m0t1fs_ior_times,  M0T1FS_ADDB_RECID_IOR_TIMES,
		uS(250), uS(500), uS(750), mS(1), mS(10), mS(50),
		mS(100), mS(250), mS(500));
M0_ADDB_RT_CNTR(m0_addb_rt_m0t1fs_iow_times,  M0T1FS_ADDB_RECID_IOW_TIMES,
		uS(250), uS(500), uS(750), mS(1), mS(10), mS(50),
		mS(100), mS(250), mS(500));
M0_ADDB_RT_CNTR(m0_addb_rt_m0t1fs_dgior_times,  M0T1FS_ADDB_RECID_DGIOR_TIMES,
		uS(250), uS(500), uS(750), mS(1), mS(10), mS(50),
		mS(100), mS(250), mS(500));
M0_ADDB_RT_CNTR(m0_addb_rt_m0t1fs_dgiow_times,  M0T1FS_ADDB_RECID_DGIOW_TIMES,
		uS(250), uS(500), uS(750), mS(1), mS(10), mS(50),
		mS(100), mS(250), mS(500));

#ifdef __KERNEL__
struct m0t1fs_addb_monitor_pfom {
	/** Periodicity of the ADDB summary stats post */
	m0_time_t             pf_period;
	/** Next post time */
	m0_time_t             pf_next_post;
	/** Shutdown/unmount request flag */
	bool                  pf_shutdown;
	/** The FOM timer */
	struct m0_fom_timeout pf_timeout;
	/** Simple FOM */
	struct m0_fom_simple  pf_fom;
};

enum m0t1fs_addb_monitor_pfom_phase {
	M0T1FS_PFOM_PHASE_CTO = M0_FOM_PHASE_NR,
	M0T1FS_PFOM_PHASE_SLEEP,
	M0T1FS_PFOM_PHASE_POST,
};

enum {
	M0T1FS_PFOM_RET_SHUTDOWN = -1,
};
#endif /* __KERNEL__ */

#endif /* __MERO_M0T1FS_M0T1FS_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
