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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_DTM

#include "lib/trace.h"
#include "lib/bob.h"
#include "lib/assert.h"
#include "lib/misc.h"                 /* M0_IN, M0_SET0 */
#include "lib/memory.h"
#include "lib/errno.h"                /* ENOMEM */
#include "lib/cookie.h"

#include "dtm/dtm_internal.h"
#include "dtm/remote.h"
#include "dtm/nucleus.h"
#include "dtm/history.h"
#include "dtm/update.h"
#include "dtm/dtm.h"

static m0_dtm_ver_t up_ver    (const struct m0_dtm_up *up);
static void sibling_persistent(struct m0_dtm_history *history,
			       struct m0_dtm_op *op, struct m0_queue *queue);
static void sibling_redo(struct m0_dtm_history *history, struct m0_dtm_op *op);
static void sibling_undo(struct m0_dtm_op *op);
static const struct m0_dtm_update_ops ch_close_ops;
static const struct m0_dtm_update_ops ch_noop_ops;

M0_INTERNAL void m0_dtm_history_init(struct m0_dtm_history *history,
				     struct m0_dtm *dtm)
{
	m0_dtm_hi_init(&history->h_hi, &dtm->d_nu);
	m0_queue_link_init(&history->h_pending);
	cat_tlink_init(history);
	m0_cookie_new(&history->h_gen);
	M0_SET0(&history->h_remcookie);
	history->h_max_ver = history->h_hi.hi_ver;
	M0_POST(m0_dtm_history_invariant(history));
}

M0_INTERNAL void m0_dtm_history_fini(struct m0_dtm_history *history)
{
	M0_PRE(m0_dtm_history_invariant(history));
	m0_queue_link_fini(&history->h_pending);
	cat_tlink_fini(history);
	m0_dtm_hi_fini(&history->h_hi);
}

M0_INTERNAL bool m0_dtm_history_invariant(const struct m0_dtm_history *history)
{
	const struct m0_dtm_history_type *htype =
		history->h_ops != NULL ? history->h_ops->hio_type : NULL;
	const struct m0_dtm_hi *hi = &history->h_hi;
	return
		m0_dtm_hi_invariant(hi) &&
		_0C(ergo(htype != NULL,
		      HISTORY_DTM(history)->d_htype[htype->hit_id] == htype)) &&
		_0C(ergo(hi->hi_flags & M0_DHF_OWNED,
			 m0_tl_forall(hi, up, &hi->hi_ups,
				      up->up_ver <= history->h_max_ver))) &&
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

	up = up0 ?: hi_tlist_tail(&history->h_hi.hi_ups);
	if (up == NULL)
		return;

	m0_queue_init(&queue);
	while (1) {
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
		M0_AMB(history, next, h_pending);
	}
	m0_queue_fini(&queue);
	M0_POST(m0_dtm_history_invariant(history));
	history_unlock(history);
}

M0_INTERNAL void m0_dtm_history_redo(struct m0_dtm_history *history,
				     m0_dtm_ver_t since)
{
	struct m0_dtm_up *up;

	history_lock(history);
	M0_PRE(m0_dtm_history_invariant(history));

	up = hi_tlist_head(&history->h_hi.hi_ups);
	if (up == NULL)
		return;

	while (1) {
		struct m0_dtm_up *prior = m0_dtm_up_prior(up);

		if (prior == NULL || (prior->up_ver != 0 &&
				      prior->up_ver <= since))
			break;
		up = prior;
	}
	up = up ?: hi_tlist_tail(&history->h_hi.hi_ups);
	while(up != NULL && up->up_state >= M0_DOS_INPROGRESS) {
		M0_ASSERT(up->up_state < M0_DOS_STABLE);
		sibling_redo(history, up->up_op);
		up = m0_dtm_up_later(up);
	}
	M0_POST(m0_dtm_history_invariant(history));
	history_unlock(history);
}

M0_INTERNAL void m0_dtm_history_undo(struct m0_dtm_history *history,
				     m0_dtm_ver_t upto)
{
	struct m0_dtm_up *up;

	history_lock(history);
	M0_PRE(m0_dtm_history_invariant(history));

	up = hi_tlist_head(&history->h_hi.hi_ups);
	while (up_ver(up) > upto) {
		struct m0_dtm_up *prior = m0_dtm_up_prior(up);

		sibling_undo(up->up_op);
		up = prior;
	}
	M0_POST(m0_dtm_history_invariant(history));
	history_unlock(history);
}

M0_INTERNAL void m0_dtm_history_close(struct m0_dtm_history *history)
{
	history_lock(history);
	history_close(history);
	history_unlock(history);
}

M0_INTERNAL void history_close(struct m0_dtm_history *history)
{
	M0_PRE(m0_dtm_history_invariant(history));
	M0_PRE(!(history->h_hi.hi_flags & M0_DHF_CLOSED));
	history->h_hi.hi_flags |= M0_DHF_CLOSED;
}

M0_INTERNAL void m0_dtm_history_update_get(const struct m0_dtm_history *history,
					   enum m0_dtm_up_rule rule,
					   struct m0_dtm_update_data *data)
{
	M0_PRE(M0_IN(rule, (M0_DUR_NOT, M0_DUR_INC, M0_DUR_SET)));

	data->da_rule  = rule;
	data->da_label = 0;
	if (history->h_hi.hi_flags & M0_DHF_OWNED) {
		data->da_orig_ver = history->h_max_ver;
		data->da_ver = rule == M0_DUR_NOT ?
			data->da_orig_ver : data->da_orig_ver + 1;
	} else {
		data->da_orig_ver = 0;
		data->da_ver      = 0;
	}
}

static void sibling_persistent(struct m0_dtm_history *history,
			       struct m0_dtm_op *op, struct m0_queue *queue)
{
	up_for(op, up) {
		struct m0_dtm_history *other;

		other = UP_HISTORY(up);
		if (up->up_state < M0_DOS_PERSISTENT && other != history &&
		    other->h_rem == history->h_rem &&
		    !m0_queue_link_is_in(&other->h_pending)) {
			M0_ASSERT(up_ver(up) >=
				  update_ver(other->h_persistent));
			other->h_persistent = up_update(up);
			m0_queue_put(queue, &other->h_pending);
		}
	} up_endfor;
}

static void sibling_redo(struct m0_dtm_history *history, struct m0_dtm_op *op)
{
	struct m0_dtm_remote *rem = history->h_rem;

	up_for(op, up) {
		struct m0_dtm_update  *update = up_update(up);
		struct m0_dtm_history *other  = UP_HISTORY(up);

		if (other->h_rem == rem && update->upd_body != NULL)
			rem->re_ops->reo_redo(rem, update);
	} up_endfor;
}

static void sibling_undo(struct m0_dtm_op *op)
{
	M0_PRE(m0_dtm_op_invariant(op));
	up_for(op, up) {
		struct m0_dtm_update  *update  = up_update(up);
		struct m0_dtm_up      *prior   = m0_dtm_up_prior(up);
		struct m0_dtm_history *history = UP_HISTORY(up);

		M0_ASSERT(up == hi_tlist_head(&up->up_hi->hi_ups));
		if (update->upd_ops->updo_undo != NULL)
			update->upd_ops->updo_undo(update);
		if (history->h_persistent == update)
			history->h_persistent = up_update(prior);
		if (history->h_max_ver == up->up_ver)
			history->h_max_ver = prior->up_ver;
		if (up->up_hi->hi_ver == up->up_ver)
			up->up_hi->hi_ver = prior->up_ver;
	} up_endfor;
	m0_dtm_op_fini(op);
}

static m0_dtm_ver_t up_ver(const struct m0_dtm_up *up)
{
	return up != NULL ? up->up_ver : 0;
}

M0_INTERNAL m0_dtm_ver_t update_ver(const struct m0_dtm_update *update)
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
m0_dtm_history_type_find(struct m0_dtm *dtm, uint8_t id)
{
	return IS_IN_ARRAY(id, dtm->d_htype) ? dtm->d_htype[id] : NULL;
}

M0_INTERNAL void m0_dtm_history_pack(const struct m0_dtm_history *history,
				     struct m0_dtm_history_id *id)
{
	id->hid_id       = *history->h_ops->hio_id(history);
	id->hid_htype    =  history->h_ops->hio_type->hit_rem_id;
	id->hid_receiver =  history->h_remcookie;
	m0_cookie_init(&id->hid_sender, &history->h_gen);
}

M0_INTERNAL int m0_dtm_history_unpack(struct m0_dtm *dtm,
				      const struct m0_dtm_history_id *id,
				      struct m0_dtm_history **out)
{
	const struct m0_dtm_history_type *htype;
	int                               result;

	htype = m0_dtm_history_type_find(dtm, id->hid_htype);
	if (htype == NULL)
		M0_RETERR(-EPROTO, "%i", id->hid_htype);

	/* !m0_cookie_is_null() && */
	*out = m0_cookie_of(&id->hid_receiver, struct m0_dtm_history, h_gen);
	result = *out != NULL ? 0 :
		htype->hit_ops->hito_find(dtm, htype, &id->hid_id, out);
	if (result == 0)
		(*out)->h_remcookie = id->hid_sender;
	return result;
}

static void control_update_add(struct m0_dtm_history *history,
			       struct m0_dtm_oper *oper,
			       struct m0_dtm_update *cupdate,
			       enum m0_dtm_up_rule rule,
			       const struct m0_dtm_update_ops *ops)
{
	struct m0_dtm_update_data udata;

	oper_lock(oper);

	M0_PRE(m0_dtm_history_invariant(history));
	M0_PRE(M0_IN(rule, (M0_DUR_NOT, M0_DUR_INC)));

	m0_dtm_history_update_get(history, rule, &udata);
	m0_dtm_update_init(cupdate, history, oper, &udata);
	cupdate->upd_ops = ops;
	M0_PRE(m0_dtm_history_invariant(history));
	oper_unlock(oper);
}

M0_INTERNAL void history_lock(const struct m0_dtm_history *history)
{
	dtm_lock(HISTORY_DTM(history));
}

M0_INTERNAL void history_unlock(const struct m0_dtm_history *history)
{
	dtm_unlock(HISTORY_DTM(history));
}

M0_INTERNAL void m0_dtm_history_add_nop(struct m0_dtm_history *history,
					struct m0_dtm_oper *oper,
					struct m0_dtm_update *cupdate)
{
	control_update_add(history, oper, cupdate, M0_DUR_INC, &ch_noop_ops);
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
	control_update_add(history, oper, cupdate, M0_DUR_INC, &ch_close_ops);
	oper->oprt_op.op_ops = &clop_ops;
	m0_dtm_oper_close(oper);
}

M0_INTERNAL void m0_dtm_controlh_init(struct m0_dtm_controlh *ch,
				      struct m0_dtm *dtm)
{
	struct m0_tl uu;

	m0_dtm_history_init(&ch->ch_history, dtm);
	m0_dtm_update_list_init(&uu);
	m0_dtm_update_link(&uu, &ch->ch_clup_rem, 1);
	m0_dtm_oper_init(&ch->ch_clop, dtm, &uu);
	m0_dtm_update_list_fini(&uu);
}

M0_INTERNAL void m0_dtm_controlh_fini(struct m0_dtm_controlh *ch)
{
	m0_dtm_oper_fini(&ch->ch_clop);
	m0_dtm_history_fini(&ch->ch_history);
}

M0_INTERNAL void m0_dtm_controlh_close(struct m0_dtm_controlh *ch)
{
	struct m0_dtm_remote *rem = ch->ch_history.h_rem;

	m0_dtm_history_add_close(&ch->ch_history, &ch->ch_clop, &ch->ch_clup);
	m0_dtm_oper_prepared(&ch->ch_clop, rem);
}

M0_INTERNAL void m0_dtm_controlh_add(struct m0_dtm_controlh *ch,
				     struct m0_dtm_oper *oper)
{
	struct m0_dtm_update *update = oper_tlist_pop(&oper->oprt_uu);

	M0_PRE(update != NULL);
	m0_dtm_history_add_nop(&ch->ch_history, oper, update);
}

enum {
	H_NOOP  = 1,
	H_CLOSE = 2
};

static int ch_noop(struct m0_dtm_update *updt)
{
	return 0;
}

static const struct m0_dtm_update_type ch_noop_utype = {
	.updtt_id   = H_NOOP,
	.updtt_name = "noop"
};

static const struct m0_dtm_update_ops ch_noop_ops = {
	.updo_redo = &ch_noop,
	.updo_undo = &ch_noop,
	.updo_type = &ch_noop_utype
};

static const struct m0_dtm_update_type ch_close_utype = {
	.updtt_id   = H_CLOSE,
	.updtt_name = "close"
};

static const struct m0_dtm_update_ops ch_close_ops = {
	.updo_redo = &ch_noop,
	.updo_undo = &ch_noop,
	.updo_type = &ch_close_utype
};

M0_INTERNAL int m0_dtm_controlh_update(struct m0_dtm_history *history,
				       uint8_t id,
				       struct m0_dtm_update *update)
{
	if (id == H_NOOP)
		update->upd_ops = &ch_noop_ops;
	else if (id == H_CLOSE)
		update->upd_ops = &ch_close_ops;
	else
		M0_RETERR(-EPROTO, "%i", id);
	return 0;
}

M0_INTERNAL void m0_dtm_controlh_fuse_close(struct m0_dtm_update *update)
{
	M0_PRE(update->upd_ops == &ch_noop_ops);
	update->upd_ops = &ch_close_ops;
	m0_dtm_history_close(UPDATE_HISTORY(update));
}

M0_INTERNAL bool
m0_dtm_controlh_update_is_close(const struct m0_dtm_update *update)
{
	M0_PRE(M0_IN(update->upd_ops, (&ch_noop_ops, &ch_close_ops)));
	return update->upd_ops == &ch_close_ops;
}

M0_INTERNAL void history_print_header(const struct m0_dtm_history *history,
				      char *buf)
{
	static struct m0_uint128 null_id = M0_UINT128(0xfe0f, 0xfe0d);
	struct m0_uint128  hid = *history->h_ops->hio_id(history);
	struct m0_uint128 *rid = history->h_rem != NULL ?
		&history->h_rem->re_id : &null_id;

	sprintf(buf, "%s@%p[%lx:%lx]->[%lx:%lx]",
		history->h_ops->hio_type->hit_name, history,
		(unsigned long)hid.u_hi, (unsigned long)hid.u_lo,
		(unsigned long)rid->u_hi, (unsigned long)rid->u_lo);
}

M0_INTERNAL void history_print(const struct m0_dtm_history *history)
{
	char buf[100];

	history_print_header(history, buf);
	M0_LOG(M0_FATAL, "history %s", &buf[0]);
	history_for(history, update) {
		update_print_internal(update,
				      update->upd_up.up_hi != &history->h_hi);
	} history_endfor;
}

#undef M0_TRACE_SUBSYSTEM

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
