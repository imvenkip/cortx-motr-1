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
 * Original creation date: 11/06/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM
#include "lib/trace.h"

#include "lib/bob.h"
#include "lib/misc.h"  /* M0_BITS */
#include "lib/errno.h" /* ENOENT EPERM */

#include "reqh/reqh.h"
#include "sm/sm.h"
#include "rpc/rpc_opcodes.h" /* M0_CM_SW_UPDATE_OPCODE */

#include "cm/sw.h"
#include "cm/cm.h"

#include "be/op.h"           /* M0_BE_OP_SYNC */

/**
   @defgroup CMSWFOM sliding window update fom
   @ingroup CMSW

   Implementation of sliding window update FOM.
   Provides mechanism to handle blocking operations like local sliding
   update and updating the persistent store with new sliding window.
   Provides interfaces to start, wakeup (if idle) and stop the sliding
   window update FOM.

   @{
*/

enum cm_sw_update_fom_phase {
	SWU_START  = M0_FOM_PHASE_INIT,
	SWU_FINI   = M0_FOM_PHASE_FINISH,
	SWU_UPDATE,
	SWU_NR
};

static const struct m0_fom_type_ops cm_sw_update_fom_type_ops = {
};

static struct m0_sm_state_descr cm_sw_update_sd[SWU_NR] = {
	[SWU_START] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "Update",
		.sd_allowed = M0_BITS(SWU_UPDATE, SWU_FINI)
	},
	[SWU_UPDATE] = {
		.sd_flags   = 0,
		.sd_name    = "Update",
		.sd_allowed = M0_BITS(SWU_FINI)
	},
	[SWU_FINI] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "Fini",
		.sd_allowed = 0
	},
};

struct m0_sm_conf cm_sw_update_conf = {
	.scf_name      = "sm: sw update conf",
	.scf_nr_states = ARRAY_SIZE(cm_sw_update_sd),
	.scf_state     = cm_sw_update_sd,
};

static struct m0_cm *cm_swu2cm(struct m0_cm_sw_update *swu)
{
	return container_of(swu, struct m0_cm, cm_sw_update);
}

static struct m0_cm_sw_update *cm_fom2swu(struct m0_fom *fom)
{
	return container_of(fom, struct m0_cm_sw_update, swu_fom);
}

static int swu_start(struct m0_cm_sw_update *swu)
{
	struct m0_cm   *cm = cm_swu2cm(swu);
	struct m0_fom  *fom = &swu->swu_fom;
	int             rc;

	m0_cm_lock(cm);
	rc = m0_cm_sw_local_update(cm);
	if (M0_IN(rc, (-ENOBUFS, -ENOENT, -ENODATA))) {
		m0_fom_phase_move(fom, 0, SWU_UPDATE);
		rc = rc == -ENOBUFS ? M0_FSO_WAIT : M0_FSO_AGAIN;
	}
	m0_cm_notify(cm);
	m0_cm_unlock(cm);

	return rc;
}

static int swu_update(struct m0_cm_sw_update *swu)
{
	struct m0_cm   *cm = cm_swu2cm(swu);
	struct m0_fom  *fom = &swu->swu_fom;
	int             rc;

	m0_cm_lock(cm);
	rc = m0_cm_sw_local_update(cm);
	if (rc == M0_FSO_WAIT)
		goto out;
	if (M0_IN(rc, (-ENOENT, -ENODATA))) {
		swu->swu_is_complete = true;
		m0_fom_phase_move(fom, 0, SWU_FINI);
	}

	rc = rc == 0 ? M0_FSO_AGAIN : M0_FSO_WAIT;
out:
	m0_cm_unlock(cm);
	return M0_RC(rc);
}

static int (*swu_action[]) (struct m0_cm_sw_update *swu) = {
	[SWU_START]  = swu_start,
	[SWU_UPDATE] = swu_update,
};

static uint64_t cm_swu_fom_locality(const struct m0_fom *fom)
{
	return fom->fo_type->ft_id;
}
static int cm_swu_fom_tick(struct m0_fom *fom)
{
	struct m0_cm_sw_update *swu;
	int                     phase = m0_fom_phase(fom);
	int                     rc;

	swu = cm_fom2swu(fom);
	rc = swu_action[phase](swu);
	if (rc < 0) {
		m0_fom_phase_move(fom, 0, SWU_FINI);
		rc = M0_FSO_WAIT;
	}

	return M0_RC(rc);
}

static void cm_swu_fom_fini(struct m0_fom *fom)
{
	m0_fom_fini(fom);
}

static const struct m0_fom_ops cm_sw_update_fom_ops = {
	.fo_fini          = cm_swu_fom_fini,
	.fo_tick          = cm_swu_fom_tick,
	.fo_home_locality = cm_swu_fom_locality
};

M0_INTERNAL void m0_cm_sw_update_init(struct m0_cm_type *cmtype)
{
	m0_fom_type_init(&cmtype->ct_swu_fomt, cmtype->ct_fom_id + 1,
			 &cm_sw_update_fom_type_ops,
			 &cmtype->ct_stype, &cm_sw_update_conf);
}

M0_INTERNAL void m0_cm_sw_update_start(struct m0_cm *cm)
{
	struct m0_cm_sw_update *swu = &cm->cm_sw_update;
	struct m0_fom          *fom = &swu->swu_fom;

	swu->swu_is_complete = false;
	m0_fom_init(&cm->cm_sw_update.swu_fom, &cm->cm_type->ct_swu_fomt,
		    &cm_sw_update_fom_ops, NULL, NULL, cm->cm_service.rs_reqh);
	m0_fom_queue(fom, cm->cm_service.rs_reqh);

	M0_LEAVE();
}

#undef M0_TRACE_SUBSYSTEM

/** @} endgroup CMSWFOM */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
