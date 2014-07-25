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

#include "ha/note_foms.h"
#include "ha/note_foms_internal.h"

#include "fop/fom_generic.h"
#include "fop/fop.h"
#include "ha/note.h"
#include "lib/memory.h"
#include "lib/trace.h"
#include "rpc/rpc.h"
#include "reqh/reqh.h"


M0_INTERNAL void m0_ha_state_set_fom_fini(struct m0_fom *fom)
{
	struct m0_ha_state_set_fom *fom_obj;

	fom_obj = container_of(fom, struct m0_ha_state_set_fom, fp_gen);
	m0_fom_fini(fom);
	m0_free(fom_obj);
}

M0_INTERNAL size_t m0_ha_state_set_fom_home_locality(const struct m0_fom *fom)
{
	M0_PRE(fom != NULL);

	return m0_fop_opcode(fom->fo_fop);
}

M0_INTERNAL void m0_ha_state_set_fom_addb_init(struct m0_fom *fom,
					       struct m0_addb_mc *mc)
{
	/**
	 * @todo: Do the actual impl, need to set
	 *          MAGIC, so that m0_fom_init() can pass
	 */
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

M0_INTERNAL int m0_ha_state_set_fom_tick(struct m0_fom *fom)
{
	struct m0_fop               *fop;
	struct m0_fop_generic_reply *rep;
	struct m0_rpc_item          *item;
	struct m0_ha_state_set_fom  *fom_obj;
	struct m0_reqh              *reqh;

	fom_obj = container_of(fom, struct m0_ha_state_set_fom, fp_gen);
	reqh = m0_fom_reqh(fom);

	fop = m0_fop_reply_alloc(fom->fo_fop, &m0_fop_generic_reply_fopt);
	if (fop != NULL) {
		return -ENOMEM;
	}
	rep = m0_fop_data(fop);
	rep->gr_rc = 0;
	rep->gr_msg.s_len = 0;

	m0_fom_block_enter(fom);
	m0_ha_state_accept(&reqh->rh_confc, m0_fop_data(fom_obj->fp_fop));
	m0_fom_block_leave(fom);

	item = m0_fop_to_rpc_item(fop);
	m0_rpc_reply_post(&fom_obj->fp_fop->f_item, item);

	m0_fom_phase_set(fom, M0_FOPH_FINISH);

	return M0_FSO_WAIT;
}

const struct m0_fom_ops m0_ha_state_set_fom_ops = {
	.fo_tick          = m0_ha_state_set_fom_tick,
	.fo_fini          = m0_ha_state_set_fom_fini,
	.fo_home_locality = m0_ha_state_set_fom_home_locality,
	.fo_addb_init     = m0_ha_state_set_fom_addb_init
};

M0_INTERNAL int m0_ha_state_set_fom_create(struct m0_fop *fop,
					   struct m0_fom **m,
					   struct m0_reqh* reqh)
{
	struct m0_fom              *fom;
	struct m0_ha_state_set_fom *fom_obj;

	M0_PRE(fop != NULL);
	M0_PRE(m != NULL);

	M0_ALLOC_PTR(fom_obj);
	if (fom_obj == NULL)
		return -ENOMEM;
	fom = &fom_obj->fp_gen;
	m0_fom_init(fom, &fop->f_type->ft_fom_type, &m0_ha_state_set_fom_ops,
			fop, NULL, reqh);
	fom_obj->fp_fop = fop;
	*m = fom;
	return 0;
}

const struct m0_fom_type_ops m0_ha_state_set_fom_type_ops = {
	.fto_create = m0_ha_state_set_fom_create
};

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
