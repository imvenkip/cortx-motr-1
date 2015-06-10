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

#include "lib/misc.h"    /* M0_BITS */
#include "fop/fom.h"     /* m0_fom_phase_outcome */
#include "mero/magic.h"  /* M0_BE_OP_SET_MAGIC */

/**
 * @addtogroup be
 *
 * @{
 */

M0_TL_DESCR_DEFINE(bos, "m0_be_op::bo_children", static,
		   struct m0_be_op, bo_set_link, bo_set_link_magic,
		   M0_BE_OP_SET_LINK_MAGIC, M0_BE_OP_SET_MAGIC);
M0_TL_DEFINE(bos, static, struct m0_be_op);

static void be_op_lock(struct m0_be_op *op)
{
	m0_sm_group_lock(op->bo_sm.sm_grp);
}

static void be_op_unlock(struct m0_be_op *op)
{
	m0_sm_group_unlock(op->bo_sm.sm_grp);
}

static bool be_op_is_locked(struct m0_be_op *op)
{
	return m0_sm_group_is_locked(op->bo_sm.sm_grp);
}

static struct m0_sm_state_descr op_states[] = {
	[M0_BOS_INIT] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "M0_SDF_INITIAL",
		.sd_allowed = M0_BITS(M0_BOS_ACTIVE),
	},
	[M0_BOS_ACTIVE] = {
		.sd_flags   = 0,
		.sd_name    = "M0_BOS_ACTIVE",
		.sd_allowed = M0_BITS(M0_BOS_DONE),
	},
	[M0_BOS_DONE] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "M0_BOS_DONE",
		.sd_allowed = 0,
	},
};

static struct m0_sm_trans_descr op_trans[] = {
	{ "started",   M0_BOS_INIT,   M0_BOS_ACTIVE },
	{ "completed", M0_BOS_ACTIVE, M0_BOS_DONE   },
};

M0_INTERNAL struct m0_sm_conf op_states_conf = {
	.scf_name      = "m0_be_op::bo_sm",
	.scf_nr_states = ARRAY_SIZE(op_states),
	.scf_state     = op_states,
	.scf_trans_nr  = ARRAY_SIZE(op_trans),
	.scf_trans     = op_trans
};

M0_INTERNAL void m0_be_op_init(struct m0_be_op *op)
{
	M0_SET0(op);	/* XXX use M0_IS0() */
	m0_sm_group_init(&op->bo_sm_group);
	m0_sm_init(&op->bo_sm, &op_states_conf, M0_BOS_INIT, &op->bo_sm_group);
	m0_sm_addb2_counter_init(&op->bo_sm);
	bos_tlist_init(&op->bo_children);
	bos_tlink_init(op);
	op->bo_parent = NULL;
	op->bo_is_op_set = false;
}

M0_INTERNAL void m0_be_op_fini(struct m0_be_op *op)
{
	bos_tlink_fini(op);
	bos_tlist_fini(&op->bo_children);
	be_op_lock(op);
	m0_sm_fini(&op->bo_sm);
	be_op_unlock(op);
	m0_sm_group_fini(&op->bo_sm_group);
}

static void be_op_set_add(struct m0_be_op *parent, struct m0_be_op *child)
{
	M0_PRE(be_op_is_locked(parent));
	M0_PRE(be_op_is_locked(child));

	bos_tlist_add_tail(&parent->bo_children, child);
	child->bo_parent = parent;
}

static void be_op_set_del(struct m0_be_op *parent, struct m0_be_op *child)
{
	M0_PRE(be_op_is_locked(parent));
	M0_PRE(child->bo_parent == parent);
	M0_PRE(bos_tlist_contains(&parent->bo_children, child));

	bos_tlist_del(child);
}

static void be_op_state_change(struct m0_be_op     *op,
                               enum m0_be_op_state  state,
                               struct m0_be_op     *child)
{
	bool state_changed = false;

	M0_PRE(M0_IN(state, (M0_BOS_ACTIVE, M0_BOS_DONE)));

	be_op_lock(op);
	M0_ASSERT(ergo(op->bo_is_op_set, child != NULL));
	M0_ASSERT(equi(child == NULL, bos_tlist_is_empty(&op->bo_children)));
	if (child != NULL && state == M0_BOS_DONE)
		be_op_set_del(op, child);
	M0_ASSERT(ergo(bos_tlist_is_empty(&op->bo_children),
		       !op->bo_is_op_set || state == M0_BOS_DONE));
	if (child == NULL ||
	    (child != NULL && ((op->bo_sm.sm_state == M0_BOS_INIT &&
	                        state == M0_BOS_ACTIVE) ||
	                       (op->bo_sm.sm_state == M0_BOS_ACTIVE &&
	                        bos_tlist_is_empty(&op->bo_children))))) {
		m0_sm_state_set(&op->bo_sm, state);
		state_changed = true;
	}
	be_op_unlock(op);

	if (op->bo_parent != NULL && state_changed)
		be_op_state_change(op->bo_parent, state, op);
}

M0_INTERNAL void m0_be_op_active(struct m0_be_op *op)
{
	M0_PRE(!op->bo_is_op_set);

	be_op_state_change(op, M0_BOS_ACTIVE, NULL);
}

M0_INTERNAL void m0_be_op_done(struct m0_be_op *op)
{
	M0_PRE(!op->bo_is_op_set);

	be_op_state_change(op, M0_BOS_DONE, NULL);
}

M0_INTERNAL bool m0_be_op_is_done(struct m0_be_op *op)
{
	return op->bo_sm.sm_state == M0_BOS_DONE;
}

M0_INTERNAL int
m0_be_op_tick_ret(struct m0_be_op *op, struct m0_fom *fom, int next_state)
{
	enum m0_fom_phase_outcome ret = M0_FSO_AGAIN;

	be_op_lock(op);
	M0_PRE(M0_IN(op->bo_sm.sm_state, (M0_BOS_ACTIVE, M0_BOS_DONE)));

	if (op->bo_sm.sm_state == M0_BOS_ACTIVE) {
		ret = M0_FSO_WAIT;
		m0_fom_wait_on(fom, &op->bo_sm.sm_chan, &fom->fo_cb);
	}
	be_op_unlock(op);

	m0_fom_phase_set(fom, next_state);
	return ret;
}

M0_INTERNAL void m0_be_op_wait(struct m0_be_op *op)
{
	struct m0_sm *sm = &op->bo_sm;

	be_op_lock(op);
	m0_sm_timedwait(sm, M0_BITS(M0_BOS_DONE), M0_TIME_NEVER);
	be_op_unlock(op);
}

M0_INTERNAL void m0_be_op_set_add(struct m0_be_op *parent,
				  struct m0_be_op *child)
{
	be_op_lock(parent);
	be_op_lock(child);

	M0_ASSERT(parent->bo_sm.sm_state != M0_BOS_DONE);
	M0_ASSERT( child->bo_sm.sm_state == M0_BOS_INIT);

	be_op_set_add(parent, child);
	parent->bo_is_op_set = true;

	be_op_unlock(child);
	be_op_unlock(parent);
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
