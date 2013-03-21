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
#include "dtm/update.h"
#include "dtm/operation.h"

M0_INTERNAL void m0_dtm_oper_init(struct m0_dtm_oper *oper)
{
	m0_dtm_op_init(&oper->oprt_op);
}

M0_INTERNAL void m0_dtm_oper_fini(struct m0_dtm_oper *oper)
{
	m0_dtm_op_fini(&oper->oprt_op);
}

M0_INTERNAL void m0_dtm_oper_close(struct m0_dtm_oper *oper)
{
	m0_dtm_op_close(&oper->oprt_op);
}

M0_INTERNAL void m0_dtm_oper_done(struct m0_dtm_oper *oper,
				  const struct m0_dtm_remote *dtm)
{
	up_for(&oper->oprt_op, up) {
		struct m0_dtm_history_remote *rem;

		rem = history_remote(hi_history(up->up_hi), dtm);
		M0_ASSERT(rem != NULL);
		rem->hr_known = up;
	} up_endfor;
	m0_dtm_op_done(&operation->oprt_op);
}

M0_INTERNAL void m0_dtm_oper_pack(const struct m0_dtm_oper *oper,
				  const struct m0_dtm_remote *dtm,
				  struct m0_dtm_oper_descr *ode)
{
	uint32_t idx = 0;

	oper_for(oper, update) {
		if (history_remote(hi_history(update->upd_up.up_hi),
				   dtm) != NULL) {
			M0_ASSERT(idx < ode->od_update.od_nr);
			m0_dtm_update_pack(update, &ode->od_update[idx++]);
		}
	} oper_endfor;
}

M0_INTERNAL void m0_dtm_oper_unpack(struct m0_dtm_oper *oper,
				    const struct m0_dtm_oper_descr *ode)
{
	uint32_t i;

	for (i = 0; i < ode->od_nr; ++i) {
		struct m0_dtm_update_descr *ud = &ode->od_update[i];
		struct m0_dtm_update       *update;

		update = m0_dtm_oper_get(oper, ud->udd_label);
		M0_ASSERT(update != NULL);
		m0_dtm_update_unpack(update, ud);
	}
}

M0_INTERNAL int m0_dtm_oper_build(struct m0_dtm_oper *oper,
				  struct m0_tlist *uu,
				  const struct m0_dtm_oper_descr *ode)
{
	uint32_t i;
	int      result;

	for (result = 0, i = 0; i < ode->od_nr; ++i) {
		struct m0_dtm_update_descr *ud = &ode->od_update[i];
		struct m0_dtm_update       *update;

		if (ud->udd_label < M0_DTM_USER_UPDATE_BASE) {
			M0_ALLOC_PTR(update);
			if (update == NULL) {
				result = -ENOMEM;
				break;
			}
		} else {
			M0_ASSERT(!update_tlist_is_empty(uu));
			update = update_tlist_head(uu);
			update_tlist_del(update);
		}
		result = m0_dtm_update_build(update, oper, ud);
		if (result != 0)
			break;
	}
	if (result != 0)
		m0_dtm_oper_fini(oper);
	M0_POST(ergo(result == 0, update_tlist_is_empty(uu)));
	return result;
}

M0_INTERNAL void m0_dtm_reply_pack(const struct m0_dtm_oper *oper,
				   const struct m0_dtm_oper_descr *request,
				   struct m0_dtm_oper_descr *reply)
{
	uint32_t i;
	uint32_t j;

	for (j = 0, i = 0; i < request->od_nr; ++i) {
		struct m0_dtm_update_descr *ud = &request->od_update[i];
		struct m0_dtm_update       *update;

		update = m0_dtm_oper_get(oper, ud->udd_label);
		M0_ASSERT(update != NULL);
		M0_ASSERT(j < reply->od_nr);
		m0_dtm_update_pack(update, &reply->od_update[j++]);
	}
}

M0_INTERNAL void m0_dtm_reply_unpack(struct m0_dtm_oper *oper,
				     const struct m0_dtm_oper_descr *reply)
{
	m0_dtm_oper_unpack(oper, ode);
}

M0_INTERNAL struct m0_dtm_update *m0_dtm_oper_get(struct m0_dtm_oper *oper,
						  uint32_t label)
{
	oper_for(oper, update) {
		if (update->upd_label == label)
			return update;
	} oper_endfor;
	return NULL;
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
