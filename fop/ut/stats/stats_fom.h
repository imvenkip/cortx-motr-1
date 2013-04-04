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
 * Original creation date: 04/04/2013
 */

#pragma once

#ifndef __MERO_FOP_UT_STATS_FOM_H__
#define __MERO_FOP_UT_STATS_FOM_H__


enum fom_stats_phase {
	PH_INIT = M0_FOM_PHASE_INIT,  /*< fom has been initialised. */
	PH_FINISH = M0_FOM_PHASE_FINISH,  /*< terminal phase. */
	PH_RUN
};

/**
 * Histogram arguments for m0_addb_rt_fom_phase_stats.
 * Define the macro with histogram arguments if desired.
 * e.g. #define FOM_PHASE_STATS_HIST_ARGS 100, 200, 500
 */
#undef FOM_PHASE_STATS_HIST_ARGS

#ifdef FOM_PHASE_STATS_HIST_ARGS
#define FOM_PHASE_STATS_HIST_ARGS2 0, FOM_PHASE_STATS_HIST_ARGS
#else
#define FOM_PHASE_STATS_HIST_ARGS2
#endif

enum {
	TRANS_NR = 3,
};

enum {
	ADDB_RECID_FOM_PHASE_STATS = 1777123,
	FOM_PHASE_STATS_DATA_SZ =
		(sizeof(struct m0_addb_counter_data) +
		  ((M0_COUNT_PARAMS(FOM_PHASE_STATS_HIST_ARGS2) > 0 ?
		    M0_COUNT_PARAMS(FOM_PHASE_STATS_HIST_ARGS2) + 1 : 0) *
                   sizeof(uint64_t))) *
		TRANS_NR
};

/**
 * Object encompassing FOM for stats
 * operation and necessary context data
 */
struct fom_stats {
	/** Generic m0_fom object. */
        struct m0_fom             fs_gen;

	/** addb sm counter for states statistics */
	struct m0_addb_sm_counter fs_phase_stats;
	/** counter data for states statistics */
	uint8_t                   fs_stats_data[FOM_PHASE_STATS_DATA_SZ];
};

#endif /* __MERO_FOP_UT_STATS_FOM_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
