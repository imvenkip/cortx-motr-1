/* -*- C -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 01/02/2014
 */

#pragma once

#ifndef __MERO_FOP_FOP_RATE_MONITOR_H__
#define __MERO_FOP_FOP_RATE_MONITOR_H__

/**
   @addtogroup fop

   This file contains definitions for fop rate monitor.
   @{
 */

enum {
	M0_FOP_RATE_MON_DATA_NR = 1
};

/**
 * Monitor data.
 */
struct m0_fop_rate_monitor {
	uint64_t                frm_magic;
	uint64_t                frm_count;
	/** Fop rate monitor data. */
	uint64_t                frm_md[M0_FOP_RATE_MON_DATA_NR];
	struct m0_addb_sum_rec  frm_sum_rec;
	struct m0_fom_locality *frm_loc;
	/** Timer to calculate fop_rate */
	struct m0_timer	        frm_timer;
	/** FOP rate counter. It is fop executed per sec. */
	struct m0_addb_counter  frm_addb_ctr;
	/** fop rate monitor */
	struct m0_addb_monitor  frm_monitor;
	/** AST to post fop_rate timer rearm */
	struct m0_sm_ast        frm_ast;
};

M0_INTERNAL int m0_fop_rate_monitor_module_init(void);

/**
 * It initialise fop rate monitor.
 * @param loc pointer to fom locality.
 * @retval 0 if sucess
 *         -ENOMEM if failed to allocate summary record.
 */
M0_INTERNAL
int m0_fop_rate_monitor_init(struct m0_fom_locality *loc);

/**
 * It finalise fop rate monitor.
 * @param loc pointer to fom locality.
 */
M0_INTERNAL
void m0_fop_rate_monitor_fini(struct m0_fom_locality *loc);

/**
 * It returns instance of fop_rate_monitor for locality "loc".
 * @param loc pointer to fom locality.
 * @retval ptr to fop_rate_monitor.
 */
M0_INTERNAL struct m0_fop_rate_monitor *
m0_fop_rate_monitor_get(struct m0_fom_locality *loc);

/** @} end of fop group */

/* __MERO_FOP_FOP_RATE_MONITOR_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
