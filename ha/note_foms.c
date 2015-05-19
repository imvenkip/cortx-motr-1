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
 * Original author: Atsuro Hoshino <atsuro_hoshino@xyratex.com>
 * Original creation date: 02-Sep-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_HA
#include "lib/trace.h"

#include "ha/note_foms.h"
#include "ha/note_foms_internal.h"

#include "fop/fom_generic.h"
#include "fop/fop.h"
#include "ha/note.h"
#include "lib/memory.h"
#include "rpc/rpc.h"
#include "reqh/reqh.h"

M0_INTERNAL void m0_ha_state_set_fom_fini(struct m0_fom *fom)
{
	m0_fom_fini(fom);
	m0_free(fom);
}

M0_INTERNAL size_t m0_ha_state_set_fom_home_locality(const struct m0_fom *fom)
{
	size_t seq = 0;
	return ++seq;
}

M0_INTERNAL int m0_ha_state_set_fom_tick(struct m0_fom *fom)
{
	m0_fom_block_enter(fom);
	m0_ha_state_accept(&m0_fom_reqh(fom)->rh_confc,
			   m0_fop_data(fom->fo_fop));
	m0_fom_block_leave(fom);
	m0_rpc_reply_post(&fom->fo_fop->f_item, &fom->fo_rep_fop->f_item);
	m0_fom_phase_set(fom, M0_FOPH_FINISH);
	return M0_FSO_WAIT;
}

const struct m0_fom_ops m0_ha_state_set_fom_ops = {
	.fo_tick          = &m0_ha_state_set_fom_tick,
	.fo_fini          = &m0_ha_state_set_fom_fini,
	.fo_home_locality = &m0_ha_state_set_fom_home_locality
};

M0_INTERNAL int m0_ha_state_set_fom_create(struct m0_fop *fop,
					   struct m0_fom **m,
					   struct m0_reqh* reqh)
{
	struct m0_fom               *fom;
	struct m0_fop               *reply;
	struct m0_fop_generic_reply *rep;

	M0_PRE(fop != NULL);
	M0_PRE(m != NULL);

	M0_ALLOC_PTR(fom);
	reply = m0_fop_reply_alloc(fop, &m0_fop_generic_reply_fopt);
	if (fom == NULL || reply == NULL) {
		m0_free(fom);
		if (reply != NULL)
			m0_fop_put_lock(reply);
		return M0_ERR(-ENOMEM);
	}
	m0_fom_init(fom, &fop->f_type->ft_fom_type, &m0_ha_state_set_fom_ops,
		    fop, reply, reqh);

	rep = m0_fop_data(fop);
	rep->gr_rc = 0;
	rep->gr_msg.s_len = 0;
	*m = fom;
	return 0;
}

const struct m0_fom_type_ops m0_ha_state_set_fom_type_ops = {
	.fto_create = &m0_ha_state_set_fom_create
};

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
