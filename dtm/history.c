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
 * @addtogroup XXX
 *
 * @{
 */

#include "lib/assert.h"

#include "dtm/nucleus.h"
#include "dtm/history.h"

M0_TL_DESCR_DEFINE(rem, "dtm remote histories", static,
		   struct m0_dtm_history_remote,
		   hr_linkage, hr_magix,
		   M0_DTM_UP_MAGIX_XXX, M0_DTM_HI_MAGIX_XXX);
M0_TL_DEFINE(rem, static, struct m0_dtm_history_remote);

M0_INTERNAL void m0_dtm_history_persistent(struct m0_dtm_history *history)
{
	struct m0_queue       queue;
	struct m0_queue_link *next;

	m0_queue_init(&queue);
	while (1) {
		struct m0_dtm_history_remote *m;
		struct m0_dtm_up             *up;
		struct m0_dtm_up             *up0;

		m  = history_min(history);
		up = up0 = update_up(m->hr_persistent);
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
}

static void sibling_persistent(struct m0_dtm_history *history,
			       struct m0_dtm_history_remote *m,
			       struct m0_dtm_op *op, struct m0_queue *queue)
{
	up_for(op, up) {
		struct m0_dtm_history *other;

		other = hi_history(up->up_hi);
		if (up->up_state < M0_DOS_PERSISTENT && other != history &&
		    !m0_queue_link_is_in(&other->hi_pending) &&
		    history_remote(other, m->hr_dtm) != NULL)
			m0_queue_put(queue, &other->hi_pending);
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
	return up_ver(update_up(rem->hr_persistent));
}

M0_INTERNAL struct m0_dtm_history *hi_history(struct m0_dtm_hi *hi)
{
	return hi != NULL ?
		container_of(hi, struct m0_dtm_history, h_hi) : NULL;
}

M0_INTERNAL struct m0_dtm_up *update_up(struct m0_dtm_update *update)
{
	return update != NULL ? &update->upd_up : NULL;
}

static struct m0_dtm_history_remote *
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

enum {
	HISTORY_TYPE_NR = 256
};

static struct m0_dtm_history_type **htype[HISTORY_TYPE_NR];

M0_INTERNAL void m0_dtm_history_type_register(struct m0_dtm_history_type *ht)
{
	M0_PRE(IS_IN_ARRAY(ht->hit_id, htype));
	M0_PRE(htype[ht->hit_id] == NULL);
	htype[ht->hit_id] = ht;
}

M0_INTERNAL void m0_dtm_history_type_deregister(struct m0_dtm_history_type *ht)
{
	M0_PRE(IS_IN_ARRAY(ht->hit_id, htype));
	M0_PRE(htype[ht->hit_id] == ht);
	htype[ht->hit_id] = NULL;
}

M0_INTERNAL const struct m0_dtm_history_type *
m0_dtm_history_type_find(uint32_t id)
{
	return IS_IN_ARRAY(id, htype) ? htype[id] : NULL;
}

/** @} end of XXX group */


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
