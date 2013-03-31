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
 * Original creation date: 27-Jan-2013
 */


/**
 * @addtogroup dtm
 *
 * @{
 */

#include "lib/errno.h"
#include "lib/memory.h"

#include "dtm/dtm_internal.h"
#include "dtm/history.h"
#include "dtm/update.h"
#include "dtm/operation.h"
#include "dtm/dtm.h"

M0_INTERNAL void m0_dtm_oper_init(struct m0_dtm_oper *oper, struct m0_dtm *dtm,
				  struct m0_tl *uu)
{
	struct m0_dtm_update *update;

	m0_dtm_op_init(&oper->oprt_op, &dtm->d_nu);
	m0_dtm_update_list_init(&oper->oprt_uu);
	if (uu != NULL) {
		m0_tl_for(oper, uu, update) {
			oper_tlist_move_tail(&oper->oprt_uu, update);
		} m0_tl_endfor;
	}
}

M0_INTERNAL void m0_dtm_oper_fini(struct m0_dtm_oper *oper)
{
	struct m0_dtm *dtm = nu_dtm(oper->oprt_op.op_nu);

	m0_dtm_update_list_fini(&oper->oprt_uu);
	dtm_lock(dtm);
	M0_PRE(m0_dtm_oper_invariant(oper));
	m0_dtm_op_fini(&oper->oprt_op);
	dtm_unlock(dtm);
}

M0_INTERNAL bool m0_dtm_oper_invariant(const struct m0_dtm_oper *oper)
{
	return
		m0_dtm_op_invariant(&oper->oprt_op) &&
		m0_tl_forall(oper, u0, &oper->oprt_op.op_ups,
			     m0_tl_forall(oper, u1, &oper->oprt_op.op_ups,
					  ergo(u0->upd_label == u1->upd_label,
					       u0 == u1)));
}

M0_INTERNAL void m0_dtm_oper_add(struct m0_dtm_oper *oper,
				 struct m0_dtm_update *update,
				 struct m0_dtm_history *history,
				 const struct m0_dtm_update_data *data)
{
	if (m0_tl_forall(oper, scan, &oper->oprt_op.op_ups,
			 UPDATE_DTM(scan) != history->h_dtm))
		m0_dtm_remote_add(history->h_dtm, oper, history, update);
	m0_dtm_update_init(update, history, oper, data);
}

M0_INTERNAL void m0_dtm_oper_close(const struct m0_dtm_oper *oper)
{
	oper_lock(oper);
	M0_PRE(m0_dtm_oper_invariant(oper));
	m0_dtm_op_close(&oper->oprt_op);
	oper_unlock(oper);
}

M0_INTERNAL void m0_dtm_oper_prepared(const struct m0_dtm_oper *oper)
{
	oper_lock(oper);
	M0_PRE(m0_dtm_oper_invariant(oper));
	m0_dtm_op_prepared(&oper->oprt_op);
	oper_unlock(oper);
}

M0_INTERNAL void m0_dtm_oper_done(const struct m0_dtm_oper *oper,
				  const struct m0_dtm_remote *dtm)
{
	oper_lock(oper);
	M0_PRE(m0_dtm_oper_invariant(oper));
	up_for(&oper->oprt_op, up) {
		M0_PRE(up->up_state == M0_DOS_INPROGRESS);
		if (UP_HISTORY(up)->h_dtm == dtm)
			up->up_state = M0_DOS_VOLATILE;
	} up_endfor;
	M0_POST(m0_dtm_oper_invariant(oper));
	oper_unlock(oper);
}

M0_INTERNAL void m0_dtm_oper_pack(const struct m0_dtm_oper *oper,
				  const struct m0_dtm_remote *dtm,
				  struct m0_dtm_oper_descr *ode)
{
	uint32_t idx = 0;

	oper_lock(oper);
	M0_PRE(m0_dtm_oper_invariant(oper));
	oper_for(oper, update) {
		if (UPDATE_DTM(update) == dtm) {
			M0_ASSERT(idx < ode->od_nr);
			m0_dtm_update_pack(update, &ode->od_update[idx++]);
		}
	} oper_endfor;
	ode->od_nr = idx;
	oper_unlock(oper);
}

M0_INTERNAL int m0_dtm_oper_build(struct m0_dtm_oper *oper, struct m0_tl *uu,
				  const struct m0_dtm_oper_descr *ode)
{
	uint32_t i;
	int      result;

	oper_lock(oper);
	M0_PRE(m0_dtm_oper_invariant(oper));
	for (result = 0, i = 0; i < ode->od_nr; ++i) {
		struct m0_dtm_update_descr *ud = &ode->od_update[i];
		struct m0_dtm_update       *update;

		update = oper_tlist_pop(uu);
		M0_ASSERT(update != NULL);
		result = m0_dtm_update_build(update, oper, ud);
		if (result != 0)
			break;
	}
	M0_POST(ergo(result == 0, oper_tlist_is_empty(uu)));
	M0_POST(ergo(result == 0, m0_dtm_oper_invariant(oper)));
	oper_unlock(oper);
	if (result != 0)
		m0_dtm_oper_fini(oper);
	return result;
}

M0_INTERNAL void m0_dtm_reply_pack(const struct m0_dtm_oper *oper,
				   const struct m0_dtm_oper_descr *request,
				   struct m0_dtm_oper_descr *reply)
{
	uint32_t i;
	uint32_t j;

	oper_lock(oper);
	M0_PRE(m0_dtm_oper_invariant(oper));
	for (j = 0, i = 0; i < request->od_nr; ++i) {
		struct m0_dtm_update_descr *ud = &request->od_update[i];
		const struct m0_dtm_update *update;

		update = m0_dtm_oper_get(oper, ud->udd_data.da_label);
		M0_ASSERT(update != NULL);
		M0_ASSERT(update->upd_up.up_state >= M0_DOS_VOLATILE);
		M0_ASSERT(m0_dtm_descr_matches_update(update, ud));
		M0_ASSERT(j < reply->od_nr);
		m0_dtm_update_pack(update, &reply->od_update[j++]);
	}
	reply->od_nr = j;
	M0_POST(m0_dtm_oper_invariant(oper));
	oper_unlock(oper);
}

M0_INTERNAL void m0_dtm_reply_unpack(struct m0_dtm_oper *oper,
				     const struct m0_dtm_oper_descr *reply)
{
	uint32_t i;

	oper_lock(oper);
	M0_PRE(m0_dtm_oper_invariant(oper));
	for (i = 0; i < reply->od_nr; ++i) {
		struct m0_dtm_update_descr *ud = &reply->od_update[i];
		struct m0_dtm_update       *update;

		update = m0_dtm_oper_get(oper, ud->udd_data.da_label);
		M0_ASSERT(update != NULL);
		M0_ASSERT(update->upd_up.up_state == M0_DOS_INPROGRESS);
		m0_dtm_update_unpack(update, ud);
	}
	M0_POST(m0_dtm_oper_invariant(oper));
	oper_unlock(oper);
}

M0_INTERNAL struct m0_dtm_update *m0_dtm_oper_get(const struct m0_dtm_oper *oper,
						  uint32_t label)
{
	M0_PRE(m0_dtm_oper_invariant(oper));
	oper_for(oper, update) {
		if (update->upd_label == label)
			return update;
	} oper_endfor;
	return NULL;
}

M0_INTERNAL void oper_lock(const struct m0_dtm_oper *oper)
{
	nu_lock(oper->oprt_op.op_nu);
}

M0_INTERNAL void oper_unlock(const struct m0_dtm_oper *oper)
{
	nu_unlock(oper->oprt_op.op_nu);
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
