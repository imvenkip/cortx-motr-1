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

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_CM

#include "lib/bob.h"
#include "lib/misc.h"  /* C2_BITS */
#include "lib/errno.h" /* ENOBUFS, ENODATA */

#include "sm/sm.h"

#include "cm/pump.h"
#include "cm/cm.h"
#include "cm/cp.h"

/**
   @addtogroup CM
   @{
*/

enum cm_cp_pump_fom_phase {
	/**
	 * New copy packets are allocated in this phase.
	 * c2_cm_cp_pump::p_fom is in CPP_ALLOC phase when it is initialised by
	 * c2_cm_cp_pump_start() and when c2_cm_sw_fill() is invoked from a copy
	 * packet FOM, during latter's finalisation, which sets the
	 * c2_cm_cp_pump::p_fom phase to CPP_ALLOC and calls c2_fom_wakeup().
	 */
	CPP_ALLOC = C2_FOM_PHASE_INIT,
	CPP_FINI  = C2_FOM_PHASE_FINISH,
	/**
	 * c2_cm_cp_pump::p_fom is transitioned to CPP_IDLE state, if no more
	 * copy packets can be created (in case buffer pool is exhausted) and
	 * in case of failure.
	 */
	CPP_IDLE,
	/**
	 * Copy packets allocated in CPP_ALLOC phase are configured in this
	 * phase.
	 */
	CPP_DATA_NEXT,
	/**
	 * Copy machine is notified about the failure, and c2_cm_cp_pump::p_fom
	 * is transitioned to CPP_IDLE state. Once copy machine handles the
	 * failure, pump FOM is resumed, else stopped if the copy machine
	 * operation is to be terminated.
	 */
	CPP_FAIL,
	CPP_NR
};

enum {
	CM_PUMP_MAGIX = 0x330FF1CE0FF1CE77,
};

static const struct c2_bob_type pump_bob = {
	.bt_name = "copy packet pump",
	.bt_magix_offset = C2_MAGIX_OFFSET(struct c2_cm_cp_pump, p_magix),
	.bt_magix = CM_PUMP_MAGIX,
	.bt_check = NULL
};

C2_BOB_DEFINE(static, &pump_bob, c2_cm_cp_pump);

static const struct c2_fom_type_ops cm_cp_pump_fom_type_ops = {
	.fto_create = NULL
};

static struct c2_fom_type cm_cp_pump_fom_type;

static struct c2_cm *pump2cm(const struct c2_cm_cp_pump *cp_pump)
{
	return container_of(cp_pump, struct c2_cm, cm_cp_pump);
}

static bool cm_cp_pump_invariant(const struct c2_cm_cp_pump *cp_pump)
{
	int phase = c2_fom_phase(&cp_pump->p_fom);

	return c2_cm_cp_pump_bob_check(cp_pump) &&
	       ergo(C2_IN(phase, (CPP_DATA_NEXT)), cp_pump->p_cp != NULL);
}

static void pump_move(struct c2_fom *fom, int rc, int phase)
{
	if (phase == CPP_FAIL)
		c2_fom_phase_move(fom, rc, phase);
	else {
		/*
		 * fom->fo_sm_phase.sm_rc can be -ENODAT or -ENOBUF which is not
		 * a failure. Thus not using c2_fom_phase_move(), but explicitly
		 * setting the error code.
		 */
		c2_fom_phase_set(fom, phase);
		c2_fom_err_set(fom, rc);
	}
}

static int cpp_alloc(struct c2_cm_cp_pump *cp_pump)
{
	struct c2_cm_cp *cp;
	struct c2_cm    *cm;
	struct c2_fom   *fom;

	fom = &cp_pump->p_fom;
	cm  = pump2cm(cp_pump);
	cp = cm->cm_ops->cmo_cp_alloc(cm);
	if (cp == NULL) {
		pump_move(fom, -ENOMEM, CPP_FAIL);
	} else {
		cp_pump->p_cp = cp;
		pump_move(fom, 0, CPP_DATA_NEXT);
	}

	return C2_FSO_AGAIN;
}

static int cpp_idle(struct c2_cm_cp_pump *cp_pump)
{
	int rc;

	/*
	 * If the pump did not fail at any point and sucessfully completed
	 * the operation, then the cp_pump->p_fom.fo_sm_phase.sm_rc == -ENODATA.
	 * Else if sns repair copy machine buffer pool runs out of buffers, then
	 * cp_pump->p_fom.fo_sm_phase.sm_rc == -ENOBUFS.
	 * We reset cp_pump->p_fom.fo_sm_phase.sm_rc = 0 in above cases, before
	 * transitioning the pump FOM into next phase.
	 */
	rc = c2_fom_rc(&cp_pump->p_fom);
	if (rc == -ENODATA || rc == -ENOBUFS)
		c2_fom_err_set(&cp_pump->p_fom, 0);

	if (cp_pump->p_shutdown) {
		pump_move(&cp_pump->p_fom, 0, CPP_FINI);
		rc = C2_FSO_WAIT;
	} else if (rc == -ENOBUFS) {
		pump_move(&cp_pump->p_fom, 0, CPP_DATA_NEXT);
		rc = C2_FSO_AGAIN;
	} else {
		pump_move(&cp_pump->p_fom, 0, CPP_ALLOC);
		rc = C2_FSO_AGAIN;
	}

	return rc;
}

static int cpp_data_next(struct c2_cm_cp_pump *cp_pump)
{
	struct c2_cm_cp  *cp;
	struct c2_cm     *cm;
	struct c2_fom    *fom = &cp_pump->p_fom;
	int               rc;

	cp = cp_pump->p_cp;
	cm = pump2cm(cp_pump);
	C2_ASSERT(cp != NULL);
	c2_cm_lock(cm);
	rc = c2_cm_data_next(cm, cp);
	if (rc < 0) {
		if (rc == -ENOBUFS || rc == -ENODATA) {
			if (rc == -ENODATA) {
				/*
				 * No more data available. Free the already
				 * allocated cp_pump->p_cp.
				 */
				cp->c_ops->co_free(cp);
				cp_pump->p_cp = NULL;
				/*
				 * No local data found corresponding to the
				 * failure. So mark the operation as complete.
				 */
				if (aggr_grps_tlist_is_empty(&cm->cm_aggr_grps))
					cm->cm_ops->cmo_complete(cm);
			}
			/*
			 * Set cp_pump->p_fom.fo_sm_phase.sm_rc = rc.
			 * Move pump FOM to CPP IDLE phase.
			 * Note: This is not a failure, but we save the rc in
			 * c2_fom::fo_sm_phase::sm_rc, for debug and later usage.
			 */
			pump_move(fom, rc, CPP_IDLE);
			cp_pump->p_is_idle = true;
			rc = C2_FSO_WAIT;
			goto out;
		}
		goto fail;
	}
	if (rc == C2_FSO_AGAIN) {
		c2_cm_cp_init(cp);
		C2_ASSERT(c2_cm_cp_invariant(cp));
		c2_cm_cp_enqueue(cm, cp);
		pump_move(fom, 0, CPP_ALLOC);
	}
	goto out;
fail:
	/* Destroy copy packet allocated in CPP_ALLOC phase. */
	cp->c_ops->co_free(cp);
	pump_move(fom, rc, CPP_FAIL);
	rc = C2_FSO_AGAIN;
out:
	c2_cm_unlock(cm);
	return rc;
}

static int cpp_fail(struct c2_cm_cp_pump *cp_pump)
{
	struct c2_cm *cm;

	C2_PRE(cp_pump != NULL);

	cm = pump2cm(cp_pump);
	c2_cm_fail(cm, C2_CM_ERR_START, c2_fom_rc(&cp_pump->p_fom));
	pump_move(&cp_pump->p_fom, 0, CPP_IDLE);

	return C2_FSO_WAIT;
}

static const struct c2_sm_state_descr cm_cp_pump_sd[CPP_NR] = {
	[CPP_ALLOC] = {
		.sd_flags   = C2_SDF_INITIAL,
		.sd_name    = "copy packet allocate",
		.sd_allowed = C2_BITS(CPP_DATA_NEXT, CPP_FAIL)
	},
	[CPP_IDLE] = {
		.sd_flags   = 0,
		.sd_name    = "copy packet pump idle",
		.sd_allowed = C2_BITS(CPP_ALLOC, CPP_DATA_NEXT, CPP_FINI)
	},
	[CPP_DATA_NEXT] = {
		.sd_flags   = 0,
		.sd_name    = "copy packet data next",
		.sd_allowed = C2_BITS(CPP_ALLOC, CPP_FAIL, CPP_IDLE)
	},
	[CPP_FAIL] = {
		.sd_flags   = 0,
		.sd_name    = "copy packet pump fail",
		.sd_allowed = C2_BITS(CPP_IDLE)
	},
	[CPP_FINI] = {
		.sd_flags   = C2_SDF_TERMINAL,
		.sd_name    = "copy packet pump finish",
		.sd_allowed = 0
	},
};

static const struct c2_sm_conf cm_cp_pump_conf = {
	.scf_name      = "sm: cp pump conf",
	.scf_nr_states = ARRAY_SIZE(cm_cp_pump_sd),
	.scf_state     = cm_cp_pump_sd
};

static int (*pump_action[]) (struct c2_cm_cp_pump *cp_pump) = {
	[CPP_ALLOC]     = cpp_alloc,
	[CPP_IDLE]	= cpp_idle,
	[CPP_DATA_NEXT] = cpp_data_next,
	[CPP_FAIL]      = cpp_fail,
};

static uint64_t cm_cp_pump_fom_locality(const struct c2_fom *fom)
{
	/*
	 * It doesn't matter which reqh locality the cp pump FOM is put into.
	 * Thus returning 0 by default.
	 */
        return 0;
}

static int cm_cp_pump_fom_tick(struct c2_fom *fom)
{
	struct c2_cm_cp_pump *cp_pump;
        int                   phase = c2_fom_phase(fom);

	cp_pump = bob_of(fom, struct c2_cm_cp_pump, p_fom, &pump_bob);
	C2_ASSERT(cm_cp_pump_invariant(cp_pump));
	return pump_action[phase](cp_pump);
}

static void cm_cp_pump_fom_fini(struct c2_fom *fom)
{
	struct c2_cm_cp_pump *cp_pump;

	c2_fom_fini(fom);
	cp_pump = bob_of(fom, struct c2_cm_cp_pump, p_fom, &pump_bob);
	c2_cm_cp_pump_bob_fini(cp_pump);
}

static const struct c2_fom_ops cm_cp_pump_fom_ops = {
	.fo_fini          = cm_cp_pump_fom_fini,
	.fo_tick          = cm_cp_pump_fom_tick,
	.fo_home_locality = cm_cp_pump_fom_locality
};


static bool pump_is_idle(const struct c2_cm_cp_pump *cp_pump)
{
	return cp_pump->p_is_idle;
}

static void pump_wakeup(struct c2_cm_cp_pump *cp_pump)
{
	cp_pump->p_is_idle = false;
	c2_fom_wakeup(&cp_pump->p_fom);
}

C2_INTERNAL void c2_cm_cp_pump_init(void)
{
	c2_fom_type_init(&cm_cp_pump_fom_type, &cm_cp_pump_fom_type_ops, NULL,
			 &cm_cp_pump_conf);
}

C2_INTERNAL void c2_cm_cp_pump_start(struct c2_cm *cm)
{
	struct c2_cm_cp_pump *cp_pump;

	C2_PRE(c2_cm_is_locked(cm));

	cp_pump = &cm->cm_cp_pump;
	c2_cm_cp_pump_bob_init(cp_pump);
	c2_fom_init(&cp_pump->p_fom, &cm_cp_pump_fom_type,
		    &cm_cp_pump_fom_ops, NULL, NULL);
	c2_fom_queue(&cp_pump->p_fom, cm->cm_service.rs_reqh);
}

C2_INTERNAL void c2_cm_cp_pump_wakeup(struct c2_cm *cm)
{
	struct c2_cm_cp_pump *cp_pump;

	C2_PRE(c2_cm_is_locked(cm));

	cp_pump = &cm->cm_cp_pump;
	if (pump_is_idle(cp_pump))
		pump_wakeup(cp_pump);
}

C2_INTERNAL void c2_cm_cp_pump_stop(struct c2_cm *cm)
{
	struct c2_cm_cp_pump *cp_pump;

	C2_PRE(c2_cm_is_locked(cm));

	cp_pump = &cm->cm_cp_pump;
	C2_ASSERT(pump_is_idle(cp_pump));
	cp_pump->p_shutdown = true;
	c2_cm_cp_pump_wakeup(cm);
}

#undef C2_TRACE_SUBSYSTEM

/** @} endgroup CM */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
