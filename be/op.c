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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 17-Jul-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/op.h"

#include "lib/misc.h"  /* M0_BITS */
#include "fop/fom.h"   /* m0_fom_phase_outcome */

/**
 * @addtogroup be
 *
 * @{
 */

/* XXX Shouldn't we pass a pointer to m0_sm_group via m0_be_op_init()
 * parameter? */
struct m0_sm_group be_op_sm_group;

static void grp_lock(const struct m0_be_op *op)
{
	m0_sm_group_lock(op->bo_sm.sm_grp);
}

static void grp_unlock(const struct m0_be_op *op)
{
	m0_sm_group_unlock(op->bo_sm.sm_grp);
}

static struct m0_sm_state_descr op_states[M0_BOS_NR] = {
	[M0_BOS_INIT] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "M0_SDF_INITIAL",
		.sd_allowed = M0_BITS(M0_BOS_ACTIVE)
	},
	[M0_BOS_ACTIVE] = {
		.sd_flags   = 0,
		.sd_name    = "M0_BOS_ACTIVE",
		.sd_allowed = M0_BITS(M0_BOS_SUCCESS, M0_BOS_FAILURE),
	},
	[M0_BOS_SUCCESS] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "M0_BOS_SUCCESS",
		.sd_allowed = 0
	},
	[M0_BOS_FAILURE] = {
		.sd_flags   = M0_SDF_FAILURE | M0_SDF_TERMINAL,
		.sd_name    = "M0_BOS_FAILURE",
		.sd_allowed = 0
	}
};

static const struct m0_sm_conf op_states_conf = {
	.scf_name      = "m0_be_op::bo_sm",
	.scf_nr_states = M0_BOS_NR,
	.scf_state     = op_states
};

M0_INTERNAL enum m0_be_op_state m0_be_op_state(const struct m0_be_op *op)
{
	return op->bo_sm.sm_state;
}

M0_INTERNAL void m0_be_op_init(struct m0_be_op *op)
{
	m0_sm_init(&op->bo_sm, &op_states_conf, M0_BOS_INIT, &be_op_sm_group);
}

M0_INTERNAL void m0_be_op_fini(struct m0_be_op *op)
{
	grp_lock(op);
	m0_sm_fini(&op->bo_sm);
	grp_unlock(op);
}

M0_INTERNAL void
m0_be_op_state_set(struct m0_be_op *op, enum m0_be_op_state state)
{
	grp_lock(op);
	m0_sm_state_set(&op->bo_sm, state);
	grp_unlock(op);
}

M0_INTERNAL int
m0_be_op_tick_ret(struct m0_be_op *op, struct m0_fom *fom, int next_state)
{
	enum m0_fom_phase_outcome ret = M0_FSO_AGAIN;

	grp_lock(op);
	M0_PRE(M0_IN(op->bo_sm.sm_state,
		     (M0_BOS_ACTIVE, M0_BOS_SUCCESS, M0_BOS_FAILURE)));

	if (op->bo_sm.sm_state == M0_BOS_ACTIVE) {
		ret = M0_FSO_WAIT;
		m0_fom_wait_on(fom, &op->bo_sm.sm_chan, &fom->fo_cb);
	}
	grp_unlock(op);

	m0_fom_phase_set(fom, next_state);
	return ret;
}

M0_INTERNAL int m0_be_op_wait(struct m0_be_op *op)
{
	struct m0_sm *sm = &op->bo_sm;

	grp_lock(op);
	m0_sm_timedwait(sm, M0_BITS(M0_BOS_SUCCESS, M0_BOS_FAILURE),
			M0_TIME_NEVER);
	grp_unlock(op);

	M0_POST((sm->sm_rc == 0) == (sm->sm_state == M0_BOS_SUCCESS));
	return sm->sm_rc;
}

/** @} end of be group */
#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
