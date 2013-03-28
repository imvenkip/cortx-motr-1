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
#include "mero/magic.h"

#include "dtm/dtm_internal.h"
#include "dtm/nucleus.h"
#include "dtm/history.h"
#include "dtm/update.h"
#include "dtm/dtm.h"

M0_TL_DESCR_DEFINE(rem, "dtm remote histories", static,
		   struct m0_dtm_history_remote,
		   hr_linkage, hr_magix,
		   M0_DTM_REMOTE_MAGIX, M0_DTM_HISTORY_REMOTE_MAGIX);

M0_TL_DEFINE(rem, static, struct m0_dtm_history_remote);

static struct m0_bob_type history_remote_bob;
M0_BOB_DEFINE(static, &history_remote_bob, m0_dtm_history_remote);

static struct m0_dtm_history_remote *history_min(struct m0_dtm_history *history);

static m0_dtm_ver_t remote_ver(const struct m0_dtm_history_remote *rem);
static m0_dtm_ver_t up_ver    (const struct m0_dtm_up *up);
static m0_dtm_ver_t update_ver(const struct m0_dtm_update *update);
static void sibling_persistent(struct m0_dtm_history *history,
			       struct m0_dtm_history_remote *m,
			       struct m0_dtm_op *op, struct m0_queue *queue);
static bool history_remote_invariant(const struct m0_dtm_history_remote *rem,
				     const struct m0_dtm_history *history);

M0_INTERNAL void m0_dtm_history_init(struct m0_dtm_history *history,
				     struct m0_dtm *dtm)
{
	m0_dtm_hi_init(&history->h_hi, &dtm->d_nu);
	m0_queue_link_init(&history->h_pending);
	m0_dtm_history_remote_init(&history->h_rem0);
	rem_tlist_init(&history->h_remote);
	M0_POST(m0_dtm_history_invariant(history));
}

M0_INTERNAL void m0_dtm_history_fini(struct m0_dtm_history *history)
{
	struct m0_dtm_history_remote *rem;

	M0_PRE(m0_dtm_history_invariant(history));
	m0_tl_for(rem, &history->h_remote, rem) {
		rem_tlist_del(rem);
	} m0_tl_endfor;
	m0_dtm_history_remote_fini(&history->h_rem0);
	rem_tlist_fini(&history->h_remote);
	m0_queue_link_fini(&history->h_pending);
	m0_dtm_hi_fini(&history->h_hi);
}

M0_INTERNAL bool m0_dtm_history_invariant(const struct m0_dtm_history *history)
{
	return
		m0_dtm_hi_invariant(&history->h_hi) &&
		m0_tl_forall(rem, rem, &history->h_remote,
			     history_remote_invariant(rem, history));
}

static bool history_remote_invariant(const struct m0_dtm_history_remote *rem,
				     const struct m0_dtm_history *history)
{
	return
		_0C(m0_dtm_history_remote_bob_check(rem)) &&
		_0C(rem->hr_history == history) &&
		_0C(update_ver(rem->hr_known) >= remote_ver(rem)) &&
		_0C(ergo(rem->hr_persistent != NULL,
		     rem->hr_persistent->upd_up.up_state >= M0_DOS_PERSISTENT));
}

M0_INTERNAL void m0_dtm_history_persistent(struct m0_dtm_history *history)
{
	struct m0_queue       queue;
	struct m0_queue_link *next;

	M0_PRE(m0_dtm_history_invariant(history));
	m0_queue_init(&queue);
	while (1) {
		struct m0_dtm_history_remote *m;
		struct m0_dtm_up             *up;
		struct m0_dtm_up             *up0;

		m  = history_min(history);
		up = up0 = UPDATE_UP(m->hr_persistent);
		while (up != NULL) {
			if (up->up_state >= M0_DOS_PERSISTENT)
				break;
			up->up_state = M0_DOS_PERSISTENT;
			sibling_persistent(history, m, up->up_op, &queue);
			up = m0_dtm_up_prior(up);
		}
		if (up_ver(up) != up_ver(up0))
			history->h_hi.hi_ops->dho_persistent(&history->h_hi,
							     up0);
		next = m0_queue_get(&queue);
		if (next == NULL)
			break;
		history = container_of(next, struct m0_dtm_history, h_pending);
	}
	m0_queue_fini(&queue);
	M0_POST(m0_dtm_history_invariant(history));
}

M0_INTERNAL void m0_dtm_history_close(struct m0_dtm_history *history)
{
	M0_PRE(m0_dtm_history_invariant(history));
	M0_PRE(!(history->h_hi.hi_flags & M0_DHF_CLOSED));
	history->h_hi.hi_flags |= M0_DHF_CLOSED;
}

static void sibling_persistent(struct m0_dtm_history *history,
			       struct m0_dtm_history_remote *m,
			       struct m0_dtm_op *op, struct m0_queue *queue)
{
	up_for(op, up) {
		struct m0_dtm_history *other;

		other = hi_history(up->up_hi);
		if (up->up_state < M0_DOS_PERSISTENT && other != history &&
		    !m0_queue_link_is_in(&other->h_pending) &&
		    history_remote(other, m->hr_dtm) != NULL)
			m0_queue_put(queue, &other->h_pending);
	} up_endfor;
}

static struct m0_dtm_history_remote *history_min(struct m0_dtm_history *history)
{
	struct m0_dtm_history_remote *m = NULL;
	struct m0_dtm_history_remote *scan;

	M0_PRE(!rem_tlist_is_empty(&history->h_remote));
	m0_tl_for(rem, &history->h_remote, scan) {
		if (m == NULL || remote_ver(m) > remote_ver(scan))
			m = scan;
	} m0_tl_endfor;
	M0_POST(m != NULL);
	return m;
}

static m0_dtm_ver_t up_ver(const struct m0_dtm_up *up)
{
	return up != NULL ? up->up_ver : 0;
}

static m0_dtm_ver_t remote_ver(const struct m0_dtm_history_remote *rem)
{
	return update_ver(rem->hr_persistent);
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

M0_INTERNAL struct m0_dtm_history_remote *
history_remote(const struct m0_dtm_history *history,
	       const struct m0_dtm_remote  *dtm)
{
	struct m0_dtm_history_remote *scan;

	m0_tl_for(rem, &history->h_remote, scan) {
		if (scan->hr_dtm == dtm)
			break;
	} m0_tl_endfor;
	return scan;
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

static int control_update_add(struct m0_dtm_history *history,
			      struct m0_dtm_oper *oper,
			      struct m0_dtm_update *cupdate,
			      enum m0_dtm_up_rule rule)
{
	int result;

	M0_PRE(M0_IN(rule, (M0_DUR_NOT, M0_DUR_INC)));

	if (cupdate == NULL)
		M0_ALLOC_PTR(cupdate);
	if (cupdate != NULL) {
		m0_dtm_ver_t              orig_ver;
		m0_dtm_ver_t              ver;

		orig_ver = history_ver(history);
		ver = rule == M0_DUR_NOT ? orig_ver : orig_ver + 1;
		m0_dtm_update_init(cupdate, history, oper,
			  &M0_DTM_UPDATE_DATA(history->h_ops->hio_type->hit_id,
					      rule, ver, orig_ver));
		result = 0;
	} else
		result = -ENOMEM;
	return result;
}

M0_INTERNAL int m0_dtm_history_add_nop(struct m0_dtm_history *history,
				       struct m0_dtm_oper *oper)
{
	M0_PRE(m0_dtm_history_invariant(history));
	return control_update_add(history, oper, NULL, M0_DUR_NOT);
}

M0_INTERNAL int m0_dtm_history_add_close(struct m0_dtm_history *history,
					 struct m0_dtm_oper *oper)
{
	M0_PRE(m0_dtm_history_invariant(history));
	m0_dtm_history_close(history);
	return control_update_add(history, oper, NULL, M0_DUR_INC);
}

M0_INTERNAL void m0_dtm_history_remote_init(struct m0_dtm_history_remote *rem)
{
	m0_dtm_history_remote_bob_init(rem);
	rem_tlink_init(rem);
}

M0_INTERNAL void m0_dtm_history_remote_fini(struct m0_dtm_history_remote *rem)
{
	rem_tlink_fini(rem);
	m0_dtm_history_remote_bob_fini(rem);
	rem->hr_history = NULL;
}

M0_INTERNAL void m0_dtm_history_add_remote(struct m0_dtm_history *history,
					   struct m0_dtm_history_remote *rem)
{
	M0_PRE(m0_dtm_history_invariant(history));
	M0_PRE(rem->hr_history == NULL);
	rem_tlist_add(&history->h_remote, rem);
	rem->hr_history = history;
	M0_POST(m0_dtm_history_invariant(history));
}

M0_INTERNAL void m0_dtm_controlh_init(struct m0_dtm_controlh *ch,
				      struct m0_dtm *dtm, struct m0_tl *uu)
{
	struct m0_dtm_update *update;

	m0_dtm_update_list_init(&ch->ch_uu);
	m0_dtm_history_init(&ch->ch_history, dtm);
	m0_dtm_oper_init(&ch->ch_clop, dtm);
	m0_tl_for(oper, uu, update) {
		oper_tlist_move_tail(&ch->ch_uu, update);
	} m0_tl_endfor;

}

M0_INTERNAL void m0_dtm_controlh_fini(struct m0_dtm_controlh *ch)
{
	m0_dtm_oper_fini(&ch->ch_clop);
	m0_dtm_history_fini(&ch->ch_history);
	m0_dtm_update_list_fini(&ch->ch_uu);
}

M0_INTERNAL void m0_dtm_controlh_close(struct m0_dtm_controlh *ch)
{
	struct m0_dtm_update *update = oper_tlist_head(&ch->ch_uu);

	M0_PRE(update != NULL);

	m0_dtm_history_close(&ch->ch_history);
	(void)control_update_add(&ch->ch_history,
				 &ch->ch_clop, update, M0_DUR_INC);
}

M0_INTERNAL void m0_dtm_controlh_add(struct m0_dtm_controlh *ch,
				     struct m0_dtm_oper *oper)
{
	struct m0_dtm_update *update = oper_tlist_head(&ch->ch_uu);

	update = oper_tlist_head(&ch->ch_uu);
	M0_PRE(update != NULL);

	(void)control_update_add(&ch->ch_history,
				 &ch->ch_clop, update, M0_DUR_NOT);
}

M0_INTERNAL void m0_dtm_history_global_init(void)
{
	m0_bob_type_tlist_init(&history_remote_bob, &rem_tl);
}

M0_INTERNAL void m0_dtm_history_global_fini(void)
{
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
