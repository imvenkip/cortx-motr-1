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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 21-Feb-2013
 */


/**
 * @addtogroup dtm
 *
 * @{
 */

#include "lib/bob.h"
#include "lib/assert.h"
#include "lib/misc.h"                 /* M0_IN */
#include "lib/memory.h"
#include "lib/errno.h"                /* ENOMEM */

#include "dtm/dtm_internal.h"
#include "dtm/nucleus.h"
#include "dtm/history.h"
#include "dtm/update.h"
#include "dtm/dtm.h"

static m0_dtm_ver_t up_ver    (const struct m0_dtm_up *up);
static m0_dtm_ver_t update_ver(const struct m0_dtm_update *update);
static void sibling_persistent(struct m0_dtm_history *history,
			       struct m0_dtm_op *op, struct m0_queue *queue);

M0_INTERNAL void m0_dtm_history_init(struct m0_dtm_history *history,
				     struct m0_dtm *dtm)
{
	m0_dtm_hi_init(&history->h_hi, &dtm->d_nu);
	m0_queue_link_init(&history->h_pending);
	M0_POST(m0_dtm_history_invariant(history));
}

M0_INTERNAL void m0_dtm_history_fini(struct m0_dtm_history *history)
{
	M0_PRE(m0_dtm_history_invariant(history));
	m0_queue_link_fini(&history->h_pending);
	m0_dtm_hi_fini(&history->h_hi);
}

M0_INTERNAL bool m0_dtm_history_invariant(const struct m0_dtm_history *history)
{
	return
		m0_dtm_hi_invariant(&history->h_hi) &&
		_0C(ergo(history->h_persistent != NULL,
		  history->h_persistent->upd_up.up_state >= M0_DOS_PERSISTENT));
}

M0_INTERNAL void m0_dtm_history_persistent(struct m0_dtm_history *history,
					   m0_dtm_ver_t upto)
{
	struct m0_queue       queue;
	struct m0_queue_link *next;
	struct m0_dtm_up     *up;
	struct m0_dtm_up     *up0 = UPDATE_UP(history->h_persistent);

	history_lock(history);

	M0_PRE(m0_dtm_history_invariant(history));
	m0_queue_init(&queue);

	up = up0 ?: hi_tlist_tail(&history->h_hi.hi_ups);
	while (up != NULL) {
		struct m0_dtm_up *later = m0_dtm_up_later(up);

		if (later == NULL || later->up_state < M0_DOS_VOLATILE ||
		    later->up_ver > upto)
			break;
		up = later;
	}
	history->h_persistent = up_update(up);
	while (1) {
		up = up0 = UPDATE_UP(history->h_persistent);
		while (up != NULL) {
			if (up->up_state >= M0_DOS_PERSISTENT)
				break;
			up->up_state = M0_DOS_PERSISTENT;
			sibling_persistent(history, up->up_op, &queue);
			up = m0_dtm_up_prior(up);
		}
		if (up_ver(up) != up_ver(up0)) {
			history->h_ops->hio_persistent(history);
			if ((history->h_hi.hi_flags & M0_DHF_CLOSED) &&
			    m0_dtm_up_later(up0) == NULL)
				history->h_ops->hio_fixed(history);
		}
		next = m0_queue_get(&queue);
		if (next == NULL)
			break;
		history = container_of(next, struct m0_dtm_history, h_pending);
	}
	m0_queue_fini(&queue);
	M0_POST(m0_dtm_history_invariant(history));
	history_unlock(history);
}

M0_INTERNAL void m0_dtm_history_close(struct m0_dtm_history *history)
{
	history_lock(history);
	M0_PRE(m0_dtm_history_invariant(history));
	M0_PRE(!(history->h_hi.hi_flags & M0_DHF_CLOSED));
	history->h_hi.hi_flags |= M0_DHF_CLOSED;
	history_unlock(history);
}

static void sibling_persistent(struct m0_dtm_history *history,
			       struct m0_dtm_op *op, struct m0_queue *queue)
{
	up_for(op, up) {
		struct m0_dtm_history *other;

		other = UP_HISTORY(up);
		if (up->up_state < M0_DOS_PERSISTENT && other != history &&
		    other->h_dtm == history->h_dtm &&
		    !m0_queue_link_is_in(&other->h_pending)) {
			M0_ASSERT(up_ver(up) >=
				  update_ver(other->h_persistent));
			other->h_persistent = up_update(up);
			m0_queue_put(queue, &other->h_pending);
		}
	} up_endfor;
}

static m0_dtm_ver_t up_ver(const struct m0_dtm_up *up)
{
	return up != NULL ? up->up_ver : 0;
}

static m0_dtm_ver_t update_ver(const struct m0_dtm_update *update)
{
	return up_ver(UPDATE_UP(update));
}

M0_INTERNAL struct m0_dtm_history *hi_history(struct m0_dtm_hi *hi)
{
	return hi != NULL ?
		container_of(hi, struct m0_dtm_history, h_hi) : NULL;
}

M0_INTERNAL void
m0_dtm_history_type_register(struct m0_dtm *dtm,
			     const struct m0_dtm_history_type *ht)
{
	M0_PRE(IS_IN_ARRAY(ht->hit_id, dtm->d_htype));
	M0_PRE(dtm->d_htype[ht->hit_id] == NULL);
	dtm->d_htype[ht->hit_id] = ht;
}

M0_INTERNAL void
m0_dtm_history_type_deregister(struct m0_dtm *dtm,
			       const struct m0_dtm_history_type *ht)
{
	M0_PRE(IS_IN_ARRAY(ht->hit_id, dtm->d_htype));
	M0_PRE(dtm->d_htype[ht->hit_id] == ht);
	dtm->d_htype[ht->hit_id] = NULL;
}

M0_INTERNAL const struct m0_dtm_history_type *
m0_dtm_history_type_find(struct m0_dtm *dtm, uint32_t id)
{
	return IS_IN_ARRAY(id, dtm->d_htype) ? dtm->d_htype[id] : NULL;
}

static m0_dtm_ver_t history_ver(const struct m0_dtm_history *history)
{
	const struct m0_dtm_up *up;

	up = hi_tlist_head(&history->h_hi.hi_ups);
	return up != NULL ? up->up_ver : 1;
}

static void control_update_add(struct m0_dtm_history *history,
			       struct m0_dtm_oper *oper,
			       struct m0_dtm_update *cupdate,
			       enum m0_dtm_up_rule rule)
{
	m0_dtm_ver_t orig_ver;
	m0_dtm_ver_t ver;

	oper_lock(oper);

	M0_PRE(m0_dtm_history_invariant(history));
	M0_PRE(M0_IN(rule, (M0_DUR_NOT, M0_DUR_INC)));

	orig_ver = history_ver(history);
	ver = rule == M0_DUR_NOT ? orig_ver : orig_ver + 1;
	m0_dtm_update_init(cupdate, history, oper,
			   &M0_DTM_UPDATE_DATA(history->h_ops->hio_type->hit_id,
					       rule, ver, orig_ver));
	oper_unlock(oper);
}

M0_INTERNAL void history_lock(const struct m0_dtm_history *history)
{
	dtm_lock(nu_dtm(history->h_hi.hi_nu));
}

M0_INTERNAL void history_unlock(const struct m0_dtm_history *history)
{
	dtm_unlock(nu_dtm(history->h_hi.hi_nu));
}

M0_INTERNAL void m0_dtm_history_add_nop(struct m0_dtm_history *history,
					struct m0_dtm_oper *oper,
					struct m0_dtm_update *cupdate)
{
	control_update_add(history, oper, cupdate, M0_DUR_NOT);
}

static void clop_nop(struct m0_dtm_op *op)
{}

static void clop_impossible(struct m0_dtm_op *op)
{
	M0_IMPOSSIBLE("Unexpected op.");
}

static const struct m0_dtm_op_ops clop_ops = {
	.doo_ready = clop_nop,
	.doo_late  = clop_impossible,
	.doo_miser = clop_impossible
};

M0_INTERNAL void m0_dtm_history_add_close(struct m0_dtm_history *history,
					  struct m0_dtm_oper *oper,
					  struct m0_dtm_update *cupdate)
{
	control_update_add(history, oper, cupdate, M0_DUR_INC);
	oper->oprt_op.op_ops = &clop_ops;
	m0_dtm_oper_close(oper);
}

M0_INTERNAL void m0_dtm_controlh_init(struct m0_dtm_controlh *ch,
				      struct m0_dtm *dtm)
{
	m0_dtm_history_init(&ch->ch_history, dtm);
	m0_dtm_oper_init(&ch->ch_clop, dtm, NULL);
}

M0_INTERNAL void m0_dtm_controlh_fini(struct m0_dtm_controlh *ch)
{
	m0_dtm_oper_fini(&ch->ch_clop);
	m0_dtm_history_fini(&ch->ch_history);
}

M0_INTERNAL void m0_dtm_controlh_close(struct m0_dtm_controlh *ch)
{
	m0_dtm_history_add_close(&ch->ch_history, &ch->ch_clop, &ch->ch_clup);
	m0_dtm_history_close(&ch->ch_history);
}

M0_INTERNAL void m0_dtm_controlh_add(struct m0_dtm_controlh *ch,
				     struct m0_dtm_oper *oper)
{
	struct m0_dtm_update *update = oper_tlist_pop(&oper->oprt_uu);

	M0_PRE(update != NULL);
	m0_dtm_history_add_nop(&ch->ch_history, oper, update);
}

M0_INTERNAL void m0_dtm_remote_add(struct m0_dtm_remote *dtm,
				   struct m0_dtm_oper *oper,
				   struct m0_dtm_history *history,
				   struct m0_dtm_update *update)
{
	m0_dtm_controlh_add(&dtm->re_fol, oper);
}

M0_INTERNAL void m0_dtm_remote_init(struct m0_dtm_remote *remote,
				    struct m0_dtm *local)
{
	m0_dtm_controlh_init(&remote->re_fol, local);
}

M0_INTERNAL void m0_dtm_remote_fini(struct m0_dtm_remote *remote)
{
	m0_dtm_controlh_fini(&remote->re_fol);
}

/** @} end of dtm group */


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
