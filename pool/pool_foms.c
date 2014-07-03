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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 06/19/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_POOL
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/misc.h"    /* M0_BITS */
#include "addb/addb.h"
#include "fop/fop.h"
#include "fop/fom_generic.h"
#include "reqh/reqh_service.h"
#include "pool/pool.h"
#include "pool/pool_foms.h"
#include "pool/pool_fops.h"
#include "ioservice/io_device.h"
#include "rpc/rpc_opcodes.h"

static const struct m0_fom_ops poolmach_ops;

static int poolmach_fom_create(struct m0_fop *fop, struct m0_fom **out,
				  struct m0_reqh *reqh)
{
	struct m0_fop *rep_fop;
	struct m0_fom *fom;
	int            rc = 0;
	M0_PRE(fop != NULL);
	M0_PRE(out != NULL);

	M0_ALLOC_PTR(fom);
	if (fom == NULL)
		return -ENOMEM;

	if (m0_fop_opcode(fop) == M0_POOLMACHINE_QUERY_OPCODE) {
		rep_fop = m0_fop_alloc(&m0_fop_poolmach_query_rep_fopt, NULL);
		M0_LOG(M0_DEBUG, "create Query fop");
	} else if (m0_fop_opcode(fop) == M0_POOLMACHINE_SET_OPCODE) {
		rep_fop = m0_fop_alloc(&m0_fop_poolmach_set_rep_fopt, NULL);
		M0_LOG(M0_DEBUG, "create set fop");
	} else {
		m0_free(fom);
		return -EINVAL;
	}
	if (rep_fop == NULL) {
		m0_free(fom);
		return -ENOMEM;
	}

	m0_fom_init(fom, &fop->f_type->ft_fom_type, &poolmach_ops, fop, rep_fop,
		    reqh);
	*out = fom;

        return rc;
}

static int poolmach_fom_tick(struct m0_fom *fom)
{
	struct m0_fop      *req_fop;
	struct m0_fop      *rep_fop;
	struct m0_poolmach *poolmach;
	struct m0_reqh      *reqh;

	reqh = m0_fom_reqh(fom);
	poolmach = m0_ios_poolmach_get(reqh);

	req_fop = fom->fo_fop;
	rep_fop = fom->fo_rep_fop;

	/* first handle generic phase */
	if (m0_fom_phase(fom) < M0_FOPH_NR) {
		/* add credit for this fom */
		if (m0_fom_phase(fom) == M0_FOPH_TXN_OPEN) {
			switch (m0_fop_opcode(req_fop)) {
			case M0_POOLMACHINE_SET_OPCODE: {
				struct m0_fop_poolmach_set *set_fop;
				int                         i;

				set_fop = m0_fop_data(req_fop);

				for (i = 0; i < set_fop->fps_dev_info.fpi_nr;
				     ++i)
					m0_poolmach_store_credit(poolmach,
							m0_fom_tx_credit(fom));
				break;
			}
			default:
				break;
			}
		}
		return m0_fom_tick_generic(fom);
	}

	switch (m0_fop_opcode(req_fop)) {
	case M0_POOLMACHINE_QUERY_OPCODE: {
		struct m0_fop_poolmach_query     *query_fop;
		struct m0_fop_poolmach_query_rep *query_fop_rep;
		int                               i;

		query_fop = m0_fop_data(req_fop);
		query_fop_rep = m0_fop_data(rep_fop);

		M0_ALLOC_ARR(query_fop_rep->fqr_dev_info.fpi_dev,
				query_fop->fpq_dev_idx.fpx_nr);
		query_fop_rep->fqr_dev_info.fpi_nr =
			query_fop->fpq_dev_idx.fpx_nr;
		for (i = 0; i < query_fop->fpq_dev_idx.fpx_nr; ++i) {
			if (query_fop->fpq_type == M0_POOL_NODE)
				m0_poolmach_node_state(poolmach,
					query_fop->fpq_dev_idx.fpx_idx[i],
                                        &query_fop_rep->fqr_dev_info.fpi_dev[i].
                                        fpd_state);
			else
				m0_poolmach_device_state(poolmach,
					query_fop->fpq_dev_idx.fpx_idx[i],
					&query_fop_rep->fqr_dev_info.fpi_dev[i].
					fpd_state);
			query_fop_rep->fqr_dev_info.fpi_dev[i].fpd_index =
				query_fop->fpq_dev_idx.fpx_idx[i];
		}
		break;
	}
	case M0_POOLMACHINE_SET_OPCODE: {
		struct m0_fop_poolmach_set     *set_fop;
		struct m0_fop_poolmach_set_rep *set_fop_rep;
		struct m0_pool_event            pme;
		int                             rc = 0;
		int                             i;

		set_fop = m0_fop_data(req_fop);
		set_fop_rep = m0_fop_data(rep_fop);

		for (i = 0; i < set_fop->fps_dev_info.fpi_nr; ++i) {
		     M0_SET0(&pme);
		     pme.pe_type  = set_fop->fps_type;
		     pme.pe_index = set_fop->fps_dev_info.fpi_dev[i].fpd_index;
		     pme.pe_state = set_fop->fps_dev_info.fpi_dev[i].fpd_state;
		     rc = m0_poolmach_state_transit(poolmach, &pme,
				     &fom->fo_tx.tx_betx);
		     if (rc != 0)
			     break;
		}
		set_fop_rep->fps_rc = rc;
		break;
	}
	default:
		M0_IMPOSSIBLE("Invalid opcode");
		m0_fom_phase_move(fom, -EINVAL, M0_FOPH_FAILURE);
		return M0_FSO_AGAIN;
	}

	m0_fom_phase_move(fom, 0, M0_FOPH_SUCCESS);
	return M0_FSO_AGAIN;
}

static size_t poolmach_fom_home_locality(const struct m0_fom *fom)
{
	M0_PRE(fom != NULL);

	return m0_fop_opcode(fom->fo_fop);
}

static void poolmach_fom_addb_init(struct m0_fom * fom, struct m0_addb_mc *mc)
{
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

static void poolmach_fom_fini(struct m0_fom *fom)
{
	m0_fom_fini(fom);
	m0_free(fom);
}

/**
 * I/O FOM operation vector.
 */
static const struct m0_fom_ops poolmach_ops = {
	.fo_fini          = poolmach_fom_fini,
	.fo_tick          = poolmach_fom_tick,
	.fo_home_locality = poolmach_fom_home_locality,
	.fo_addb_init     = poolmach_fom_addb_init
};

/**
 * I/O FOM type operation vector.
 */
const struct m0_fom_type_ops poolmach_fom_type_ops = {
	.fto_create = poolmach_fom_create
};

struct m0_sm_state_descr poolmach_phases[] = {
	[M0_FOPH_POOLMACH_FOM_EXEC] = {
		.sd_name    = "Pool Machine query/set",
		.sd_allowed = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE)
	}
};

struct m0_sm_conf poolmach_conf = {
	.scf_name      = "poolmach phases",
	.scf_nr_states = ARRAY_SIZE(poolmach_phases),
	.scf_state     = poolmach_phases
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
