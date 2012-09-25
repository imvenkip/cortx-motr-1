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
 * Original creation date: 25/09/2011
 */

#pragma once

#ifndef __COLIBRI_CM_PUMP_H__
#define __COLIBRI_CM_PUMP_H__

#include "fop/fom.h"

/**
   @addtogroup CM
 */

/* Import */
struct c2_cm;

/**
 * Represents copy packet pump FOM. New copy packets are created in context
 * of cm_cp_pump::p_fom. The pump FOM (cm_cp_pump::p_fom) nicely resolves
 * the issues with creation of new copy packets and configuring them using
 * c2_cm_data_next(), which may block. The pump FOM is created when copy machine
 * operation starts and finalised when copy machine operation is complete.
 * The pump FOM goes to sleep when no more copy packets can be created (buffer
 * pool is exhausted). When a copy packet FOM terminates and frees its buffer
 * in the pool, it wakes up the pump FOM (using c2_cm_sw_fill()) to create more
 * copy packets.
 */
struct c2_cm_cp_pump {
	/** pump FOM. */
	struct c2_fom    p_fom;
	/**
	 * Saved copy packet, in-case the pump FOM goes into wait state due to
	 * c2_cm_data_next().
	 */
	struct c2_cm_cp *p_cp;
	uint64_t         p_magix;
};

void c2_cm_cp_pump_init(void);
void c2_cm_cp_pump_start(struct c2_cm *cm);
void c2_cm_cp_pump_stop(struct c2_cm *cm);
void c2_cm_cp_pump_wakeup(struct c2_cm *cm);

/** @} endgroup CM */

/* __COLIBRI_CM_PUMP_H__ */

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
