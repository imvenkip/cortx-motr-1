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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM
#include "lib/trace.h"

#include "lib/bob.h"
#include "lib/misc.h"  /* M0_BITS */
#include "lib/errno.h" /* ENOBUFS, ENODATA */

#include "sm/sm.h"
#include "rpc/rpc_opcodes.h" /* M0_CM_PUMP_OPCODE */

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
	 * m0_cm_cp_pump::p_fom is in CPP_ALLOC phase when it is initialised by
	 * m0_cm_cp_pump_start() and when m0_cm_sw_fill() is invoked from a copy
	 * packet FOM, during latter's finalisation, which sets the
	 * m0_cm_cp_pump::p_fom phase to CPP_ALLOC and calls m0_fom_wakeup().
	 */
	CPP_ALLOC = M0_FOM_PHASE_INIT,
	CPP_FINI  = M0_FOM_PHASE_FINISH,
	/**
	 * Copy packets allocated in CPP_ALLOC phase are configured in this
	 * phase.
	 */
	CPP_DATA_NEXT,
	/**
	 * m0_cm_cp_pump::p_fom is transitioned to CPP_WAIT when
	 * m0_cm_data_next() returns -ENOBUFS (i.e no free buffers available at
	 * this moment) in CPP_DATA_NEXT phase. The pump FOM goes to wait in
	 * this phase and is woken up once the buffers are available.
	 * Pump fom could also go to wait state while waiting for other async
	 * operations.
	 */
	CPP_WAIT,
	/**
	 * m0_cm_cp_pump::p_fom is transitioned to CPP_COMPLETE phase, once
	 * m0_cm_data_next() returns -ENODATA (i.e. there's no more data to
	 * process for the iterator).
	 */
	CPP_COMPLETE,
	/**
	 * Copy machine is notified about the failure, and m0_cm_cp_pump::p_fom
	 * remains in CPP_FAIL state. Once copy machine handles the failure
	 * pump FOM is resumed, else stopped if the copy machine operation is to
	 * be terminated.
	 */
	CPP_FAIL,
	CPP_NR
};

enum {
	CM_PUMP_MAGIX = 0x330FF1CE0FF1CE77,
};

static const struct m0_bob_type pump_bob = {
	.bt_name = "copy packet pump",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_cm_cp_pump, p_magix),
	.bt_magix = CM_PUMP_MAGIX,
	.bt_check = NULL
};

M0_BOB_DEFINE(static, &pump_bob, m0_cm_cp_pump);

static const struct m0_fom_type_ops cm_cp_pump_fom_type_ops = {
	.fto_create = NULL
};

static struct m0_cm *pump2cm(const struct m0_cm_cp_pump *cp_pump)
{
	return container_of(cp_pump, struct m0_cm, cm_cp_pump);
}

static bool cm_cp_pump_invariant(const struct m0_cm_cp_pump *cp_pump)
{
	int phase = m0_fom_phase(&cp_pump->p_fom);

	return m0_cm_cp_pump_bob_check(cp_pump) &&
	       ergo(M0_IN(phase, (CPP_DATA_NEXT)), cp_pump->p_cp != NULL);
}

static void pump_move(struct m0_cm_cp_pump *cp_fom, int rc, int phase)
{
	m0_fom_phase_move(&cp_fom->p_fom, rc, phase);
}

static int cpp_alloc(struct m0_cm_cp_pump *cp_pump)
{
	struct m0_cm_cp *cp;
	struct m0_cm    *cm;

	cm  = pump2cm(cp_pump);
	cp = cm->cm_ops->cmo_cp_alloc(cm);
	if (cp == NULL) {
		pump_move(cp_pump, -ENOMEM, CPP_FAIL);
	} else {
		m0_cm_cp_fom_init(cm, cp);
		cp_pump->p_cp = cp;
		pump_move(cp_pump, 0, CPP_DATA_NEXT);
	}

	return M0_FSO_AGAIN;
}

static int cpp_data_next(struct m0_cm_cp_pump *cp_pump)
{
	struct m0_cm_cp  *cp;
	struct m0_cm     *cm;
	int               rc;
	M0_ENTRY("pump = %p", cp_pump);

	cp = cp_pump->p_cp;
	cm = pump2cm(cp_pump);
	M0_ASSERT(cp != NULL);
	m0_cm_lock(cm);

	/* This operation might block. */
	rc = m0_cm_data_next(cm, cp);
	if (rc == M0_FSO_WAIT) {
		pump_move(cp_pump, 0, CPP_WAIT);
		goto wait;
	}

	if (rc < 0) {
		if (rc == -ENOBUFS) {
			pump_move(cp_pump, 0, CPP_WAIT);
			goto wait;
		}

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
			if (m0_cm_aggr_group_tlists_are_empty(cm))
				m0_cm_complete(cm);
			pump_move(cp_pump, 0, CPP_COMPLETE);
			M0_LOG(M0_DEBUG, "pump moves to COMPLETE");
			goto wait;
		}
		goto fail;
	}
	if (rc == M0_FSO_AGAIN) {
		m0_cm_cp_enqueue(cm, cp);
		pump_move(cp_pump, 0, CPP_ALLOC);
	}
	goto out;
fail:
	/* Destroy copy packet allocated in CPP_ALLOC phase. */
	cp->c_ops->co_free(cp);
	pump_move(cp_pump, rc, CPP_FAIL);
	rc = M0_FSO_AGAIN;
	goto out;

wait:
	rc = M0_FSO_WAIT;
	M0_LOG(M0_DEBUG, "Now pump is idle. WAIT");

out:
	m0_cm_unlock(cm);
	return M0_RC(rc);
}

static int cpp_wait(struct m0_cm_cp_pump *cp_pump)
{
	pump_move(cp_pump, 0, CPP_DATA_NEXT);
	return M0_FSO_AGAIN;
}

static int cpp_complete(struct m0_cm_cp_pump *cp_pump)
{
	if (cp_pump->p_shutdown)
		pump_move(cp_pump, 0, CPP_FINI);

	return M0_FSO_WAIT;
}

static int cpp_fail(struct m0_cm_cp_pump *cp_pump)
{
	struct m0_cm *cm;

	M0_PRE(cp_pump != NULL);

	cm = pump2cm(cp_pump);
	m0_cm_lock(cm);
	m0_cm_fail(cm, M0_CM_ERR_START, m0_fom_rc(&cp_pump->p_fom));
	m0_cm_unlock(cm);
	pump_move(cp_pump, 0, CPP_FINI);
	return M0_FSO_WAIT;
}

static struct m0_sm_state_descr cm_cp_pump_sd[CPP_NR] = {
	[CPP_ALLOC] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "copy packet allocate",
		.sd_allowed = M0_BITS(CPP_DATA_NEXT, CPP_FAIL)
	},
	[CPP_DATA_NEXT] = {
		.sd_flags   = 0,
		.sd_name    = "copy packet data next",
		.sd_allowed = M0_BITS(CPP_ALLOC, CPP_WAIT, CPP_COMPLETE,
				CPP_FAIL)
	},
	[CPP_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "copy packet data next",
		.sd_allowed = M0_BITS(CPP_DATA_NEXT, CPP_FAIL)
	},
	[CPP_COMPLETE] = {
		.sd_flags   = 0,
		.sd_name    = "copy packet data next",
		.sd_allowed = M0_BITS(CPP_ALLOC, CPP_FINI)
	},
	[CPP_FAIL] = {
		.sd_flags   = M0_SDF_FAILURE,
		.sd_name    = "copy packet pump fail",
		.sd_allowed = M0_BITS(CPP_FINI)
	},
	[CPP_FINI] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "copy packet pump finish",
		.sd_allowed = 0
	},
};

const static struct m0_sm_conf cm_cp_pump_conf = {
	.scf_name      = "sm: cp pump conf",
	.scf_nr_states = ARRAY_SIZE(cm_cp_pump_sd),
	.scf_state     = cm_cp_pump_sd
};

static int (*pump_action[]) (struct m0_cm_cp_pump *cp_pump) = {
	[CPP_ALLOC]     = cpp_alloc,
	[CPP_DATA_NEXT] = cpp_data_next,
	[CPP_WAIT]      = cpp_wait,
	[CPP_COMPLETE]  = cpp_complete,
	[CPP_FAIL]      = cpp_fail,
};

static uint64_t cm_cp_pump_fom_locality(const struct m0_fom *fom)
{
	return 0;
}

static void _pump_wait(struct m0_cm_cp_pump *cp_pump)
{
	M0_ENTRY("cp_pump = %p", cp_pump);

	m0_mutex_lock(&cp_pump->p_signal_mutex);
	m0_fom_wait_on(&cp_pump->p_fom, &cp_pump->p_signal,
			&cp_pump->p_fom.fo_cb);
	m0_mutex_unlock(&cp_pump->p_signal_mutex);
}

M0_INTERNAL void m0_cm_cp_pump_wakeup(struct m0_cm *cm)
{
	M0_ENTRY("cm = %p", cm);

	m0_chan_signal_lock(&cm->cm_cp_pump.p_signal);

	M0_LEAVE();
}

static int cm_cp_pump_fom_tick(struct m0_fom *fom)
{
	struct m0_cm_cp_pump *cp_pump;
        int                   phase = m0_fom_phase(fom);
	int                   rc;

	cp_pump = bob_of(fom, struct m0_cm_cp_pump, p_fom, &pump_bob);
	M0_ASSERT(cm_cp_pump_invariant(cp_pump));
	rc = pump_action[phase](cp_pump);
	if (rc == M0_FSO_WAIT && m0_fom_phase(fom) != CPP_FINI &&
	    !m0_fom_is_waiting_on(fom))
		_pump_wait(cp_pump);
	return M0_RC(rc);
}

static void cm_cp_pump_fom_fini(struct m0_fom *fom)
{
	struct m0_cm_cp_pump *cp_pump;

	cp_pump = bob_of(fom, struct m0_cm_cp_pump, p_fom, &pump_bob);
	m0_cm_cp_pump_bob_fini(cp_pump);
	m0_fom_fini(fom);
}

static const struct m0_fom_ops cm_cp_pump_fom_ops = {
	.fo_fini          = cm_cp_pump_fom_fini,
	.fo_tick          = cm_cp_pump_fom_tick,
	.fo_home_locality = cm_cp_pump_fom_locality
};

bool m0_cm_cp_pump_is_complete(const struct m0_cm_cp_pump *cp_pump)
{
	return m0_fom_phase(&cp_pump->p_fom) == CPP_COMPLETE;
}

M0_INTERNAL void m0_cm_cp_pump_init(struct m0_cm_type *cmtype)
{
	m0_fom_type_init(&cmtype->ct_pump_fomt, cmtype->ct_fom_id + 2,
			 &cm_cp_pump_fom_type_ops,
			 &cmtype->ct_stype, &cm_cp_pump_conf);
}

M0_INTERNAL void m0_cm_cp_pump_prepare(struct m0_cm *cm)
{
	struct m0_cm_cp_pump *cp_pump;
	M0_ENTRY("cm = %p", cm);

	M0_PRE(m0_cm_is_locked(cm));

	cp_pump = &cm->cm_cp_pump;
	m0_cm_cp_pump_bob_init(cp_pump);
	m0_mutex_init(&cp_pump->p_signal_mutex);
	m0_chan_init(&cp_pump->p_signal, &cp_pump->p_signal_mutex);
	cp_pump->p_shutdown = false;
	m0_fom_init(&cp_pump->p_fom, &cm->cm_type->ct_pump_fomt,
		    &cm_cp_pump_fom_ops, NULL, NULL, cm->cm_service.rs_reqh);
}

M0_INTERNAL void m0_cm_cp_pump_start(struct m0_cm *cm)
{
	struct m0_cm_cp_pump *cp_pump;
	M0_ENTRY("cm = %p", cm);

	M0_PRE(m0_cm_is_locked(cm));

	cp_pump = &cm->cm_cp_pump;
	m0_fom_queue(&cp_pump->p_fom, cm->cm_service.rs_reqh);
	M0_LEAVE();
}

M0_INTERNAL void m0_cm_cp_pump_stop(struct m0_cm *cm)
{
	struct m0_cm_cp_pump *cp_pump;
	M0_ENTRY("cm = %p", cm);

	M0_PRE(m0_cm_is_locked(cm));

	cp_pump = &cm->cm_cp_pump;
	cp_pump->p_shutdown = true;
	m0_cm_cp_pump_wakeup(cm);
	M0_LEAVE();
}

#undef M0_TRACE_SUBSYSTEM

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
