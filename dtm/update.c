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
 * Original creation date: 01-Feb-2013
 */


/**
 * @addtogroup dtm
 *
 * @{
 */

#include "lib/assert.h"
#include "lib/errno.h"                  /* EPROTO */

#include "dtm/dtm_internal.h"
#include "dtm/nucleus.h"
#include "dtm/operation.h"
#include "dtm/history.h"
#include "dtm/update.h"

M0_INTERNAL void m0_dtm_update_init(struct m0_dtm_update *update,
				    struct m0_dtm_history *history,
				    struct m0_dtm_oper *oper,
				    enum m0_dtm_up_rule rule,
				    m0_dtm_ver_t ver, m0_dtm_ver_t orig_ver)
{
	M0_PRE(m0_tl_forall(update, upd, &oper->oprt_op.op_ups,
			    upd->upd_label != update->upd_label));
	m0_dtm_up_init(&update->upd_up, &history->h_hi, &oper->oprt_op,
		       rule, ver, orig_ver);
}

M0_INTERNAL bool m0_dtm_update_is_user(const struct m0_dtm_update *update)
{
	return update->upd_label >= M0_DTM_USER_UPDATE_BASE;
}

M0_INTERNAL void m0_dtm_update_pack(const struct m0_dtm_update *update,
				    struct m0_dtm_update_descr *updd)
{
	struct m0_dtm_up      *up      = &update->upd_up;
	struct m0_dtm_history *history = hi_history(update->upd_up.up_hi);

	*updd = (struct m0_dtm_update_descr) {
		.udd_label    = update->upd_label,
		.udd_rule     = up->up_rule,
		.udd_ver      = up->up_ver,
		.udd_orig_ver = up->up_origver
	};
	history->h_ops->hio_id(history, &updd->udd_id);
}

M0_INTERNAL void m0_dtm_update_unpack(struct m0_dtm_update *update,
				      const struct m0_dtm_update_descr *updd)
{
	struct m0_dtm_up *up = &update->upd_up;

	M0_PRE(m0_uint128_eq(update->upd_label, &updd->udd_label));

	up->up_rule     = updd->udd_rule;
	up->up_ver      = updd->udd_ver;
	up->up_orig_ver = updd->udd_orig_ver;
}

M0_INTERNAL int m0_dtm_update_build(struct m0_dtm_update *update,
				    struct m0_dtm_oper *oper,
				    const struct m0_dtm_update_descr *updd)
{
	const struct m0_dtm_history_type *htype;
	struct m0_dtm_history            *history;
	int                               result;

	if (m0_tl_exists(update, scan, &oper->oprt_op.op_ups,
			 scan->upd_label == update->upd_label))
		return -EPROTO;

	htype = m0_dtm_history_type_find(updd->udd_htype);
	if (htype == NULL)
		return -EPROTO;

	result = htype->hit_ops->hito_find(htype, &updd->udd_id, &history);
	if (result == 0) {
		update->upd_label = updd->udd_label;
		m0_dtm_update_init(update, history, oper, updd->udd_rule,
				   updd->udd_ver, updd->udd_orig_ver);
	}
	return result;
}

M0_TL_DESCR_DEFINE(history, "dtm history updates", M0_INTERNAL,
		   struct m0_dtm_update,
		   upd_up.up_hi_linkage, upd_up.up_magix,
		   M0_DTM_UP_MAGIX, M0_DTM_HI_MAGIX);
M0_TL_DEFINE(history, M0_INTERNAL, struct m0_dtm_update);

M0_TL_DESCR_DEFINE(oper, "dtm operation updates", M0_INTERNAL,
		   struct m0_dtm_update,
		   upd_up.up_op_linkage, upd_up.up_magix,
		   M0_DTM_UP_MAGIX, M0_DTM_OP_MAGIX);
M0_TL_DEFINE(oper, M0_INTERNAL, struct m0_dtm_up);



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
