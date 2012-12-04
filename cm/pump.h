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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 09/25/2011
 */

#pragma once

#ifndef __MERO_CM_PUMP_H__
#define __MERO_CM_PUMP_H__

#include "fop/fom.h"

/**
   @addtogroup CM
 */

/* Import */
struct m0_cm;

/**
 * Represents copy packet pump FOM. New copy packets are created in context
 * of cm_cp_pump::p_fom. The pump FOM (cm_cp_pump::p_fom) nicely resolves
 * the issues with creation of new copy packets and configuring them using
 * m0_cm_data_next(), which may block. The pump FOM is created when copy machine
 * operation starts and finalised when copy machine operation is complete.
 * The pump FOM goes to sleep when no more copy packets can be created (buffer
 * pool is exhausted). When a copy packet FOM terminates and frees its buffer
 * in the pool, it wakes up the pump FOM (using m0_cm_sw_fill()) to create more
 * copy packets.
 */
struct m0_cm_cp_pump {
	/** pump FOM. */
	struct m0_fom    p_fom;
	/**
	 * Every newly allocate Copy packet in CPP_ALLOC phase is saved for the
	 * further references, until the CPP_DATA_NEXT phase is completed for
	 * the copy packet. Pump FOM does not free this allocated copy packet,
	 * it is freed as part of copy packet FOM finalisation.
	 */
	struct m0_cm_cp *p_cp;
	uint64_t         p_magix;
	/** Set true by m0_cm_cp_pump_stop() */
	bool		 p_shutdown;
	/**
	 * Set true when m0_cm_cp_pump::p_fom is transitioned to CPP_IDLE phase,
	 * and set to false by m0_pump_fom_wakeup().
	 * This is used to check if pump is idle instead of doing
	 * m0_fom_phase(p_fom) == CPP_IDLE, because of the following situation,
	 * As multiple copy packet FOMs can be finalised concurrently and thus
	 * invoke m0_cm_sw_fill(), this invokes m0_cm_cp_pump_wakeup(), which
	 * posts a struct m0_sm_ast to wakeup pump FOM, thus multiple asts are
	 * posted to wakeup the same pump FOM, which are executed at the same
	 * time later, all trying to wakeup pump FOM. So setting p_is_idle true
	 * before invoking m0_fom_wakeup() in m0_cm_cp_pump_wakeup() avoids this
	 * race.
	 */
	bool             p_is_idle;
};

M0_INTERNAL void m0_cm_cp_pump_init(void);

/**
 * Initialises pump FOM and submits it to reqh for processing.
 * This is invoked from m0_cm_start()
 */
M0_INTERNAL void m0_cm_cp_pump_start(struct m0_cm *cm);

/**
 * Stops pump FOM by setting m0_cm_cp_pump::p_shutdown to true and awaking
 * pump FOM using m0_fom_wakeup().
 */
M0_INTERNAL void m0_cm_cp_pump_stop(struct m0_cm *cm);

/**
 * Wakes up pump FOM, to create more copy packets iff m0_cm_cp_pump::p_is_idle.
 * Resets m0_cm_cp_pump::p_is_idle to false.
 */
M0_INTERNAL void m0_cm_cp_pump_wakeup(struct m0_cm *cm);

/** @} endgroup CM */

/* __MERO_CM_PUMP_H__ */

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
