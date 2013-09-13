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
 * Original creation date: 12/22/2012
 */

#pragma once

#ifndef __MERO_FOP_FOP_ADDB_H__
#define __MERO_FOP_FOP_ADDB_H__

#include "addb/addb.h"
#include "fop/fom.h"   /* FOM_STATE_STATS_HIST_ARGS */

/**
   @addtogroup fop
   @{
 */

/*
 ******************************************************************************
 * FOP/FOM ADDB context types
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_FOP_MOD = 1400,
	M0_ADDB_CTXID_FOM_LOCALITY = 1420,
};

M0_ADDB_CT(m0_addb_ct_fop_mod, M0_ADDB_CTXID_FOP_MOD);
M0_ADDB_CT(m0_addb_ct_fom_locality, M0_ADDB_CTXID_FOM_LOCALITY, "cpu_id");

/**
   fop/fom function failure macro using the global ADDB machine to post.
   @param rc Return code
   @param loc Location code - one of the FOP_ADDB_LOC_ enumeration constants
   @param ctx Runtime context pointer
   @pre rc < 0
 */
#define FOP_ADDB_FUNCFAIL(rc, loc, ctx)		                     \
M0_ADDB_FUNC_FAIL(&m0_addb_gmc, M0_FOP_ADDB_LOC_##loc, rc, ctx)

#define FOP_ALLOC_PTR(ptr, loc, ctx)			             \
M0_ALLOC_PTR_ADDB(ptr, &m0_addb_gmc, M0_FOP_ADDB_LOC_##loc, ctx)

#define FOP_ALLOC_ARR(ptr, nr, loc, ctx)                             \
M0_ALLOC_ARR_ADDB(ptr, nr, &m0_addb_gmc, M0_FOP_ADDB_LOC_##loc, ctx)

/*
 ******************************************************************************
 * fop/fom ADDB posting locations
 ******************************************************************************
 */
enum {
	M0_FOP_ADDB_LOC_LOC_THR_CREATE    = 10,
	M0_FOP_ADDB_LOC_FOM_DOMAIN_INIT   = 20,
	M0_FOP_ADDB_LOC_FOM_RATE_MON_INIT = 30,
};

extern struct m0_addb_ctx m0_fop_addb_ctx;

/** @} */ /* end of fop group */

/**
 * @addtogroup fom
 * @{
 */

/**
 * ADDB record types
 * Do not change the numbering.
 */
enum {
	M0_ADDB_RECID_FOM_INIT            = 60,
	M0_ADDB_RECID_FOM_FINI            = 61,
	M0_ADDB_RECID_FOM_STATE_STATS     = 62,
	M0_ADDB_RECID_FL_RUN_TIMES        = 65,
	M0_ADDB_RECID_FL_SCHED_WAIT_TIMES = 66,
	M0_ADDB_RECID_FL_RUNQ_NR          = 67,
	M0_ADDB_RECID_FL_WAIL_NR          = 68,
	M0_ADDB_RECID_FOP_RATE_CNTR       = 69,
	M0_ADDB_RECID_FOP_RATE            = 70,
};

/**
 * ADDB Function failure locations.
 */
enum {
	M0_FOP_ADDB_LOC_GENERIC_REPLY_BUILD = 70
};

/**
 * FOM init datapoint record
 */
M0_ADDB_RT_DP(m0_addb_rt_fom_init, M0_ADDB_RECID_FOM_INIT);

/**
 * FOM fini datapoint record
 *
 * @param transitions FOM transitions number
 */
M0_ADDB_RT_DP(m0_addb_rt_fom_fini, M0_ADDB_RECID_FOM_FINI, "transitions");

extern struct m0_sm_conf fom_states_conf;

#ifdef FOM_STATE_STATS_HIST_ARGS
/** FOM state statistics counter */
M0_ADDB_RT_SM_CNTR(m0_addb_rt_fom_state_stats, M0_ADDB_RECID_FOM_STATE_STATS,
		   &fom_states_conf, FOM_STATE_STATS_HIST_ARGS);
#else
M0_ADDB_RT_SM_CNTR(m0_addb_rt_fom_state_stats, M0_ADDB_RECID_FOM_STATE_STATS,
		   &fom_states_conf);
#endif

/**
 * Accumulated run time of all locality foms (in "binary" usec).
 * "Binary usec" == (nsec >> 10).
 */
M0_ADDB_RT_CNTR(m0_addb_rt_fl_run_times, M0_ADDB_RECID_FL_RUN_TIMES);
/** Accumulated scheduling overhead of all locality foms (in binary usec). */
M0_ADDB_RT_CNTR(m0_addb_rt_fl_sched_wait_times,
		M0_ADDB_RECID_FL_SCHED_WAIT_TIMES);
/** FOP rate (number of fop executed per sec) **/
M0_ADDB_RT_CNTR(m0_addb_rt_fop_rate_cntr, M0_ADDB_RECID_FOP_RATE_CNTR);

/** Locality run queue length */
M0_ADDB_RT_DP(m0_addb_rt_fl_runq_nr, M0_ADDB_RECID_FL_RUNQ_NR, "runq_nr");
/** Locality wait queue (list) length */
M0_ADDB_RT_DP(m0_addb_rt_fl_wail_nr, M0_ADDB_RECID_FL_WAIL_NR, "wail_nr");
/** ADDB summary record for fop rate stats */
M0_ADDB_RT_STATS(m0_addb_rt_fop_rate, M0_ADDB_RECID_FOP_RATE, "fop_rate");

/** @} end of fom group */

#endif /* __MERO_FOP_FOP_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
