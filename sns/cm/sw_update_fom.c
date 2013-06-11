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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 03/08/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/misc.h"           /* M0_IN() */

#include "fop/fop.h"
#include "fop/fop_item_type.h"
#include "fop/fom.h"
#include "rpc/rpc.h"
#include "rpc/rpc_opcodes.h"

#include "cm/proxy.h"
#include "cm/cm.h"
#include "sns/cm/sw_update_fop.h"
#include "sns/cm/sw_update_fom.h"

/**
   @addtogroup CMREADY

   @{
 */

static struct m0_sm_state_descr sw_update_fom_phases[] = {
	[SWUPH_START] = {
		.sd_flags     = M0_SDF_INITIAL,
		.sd_name      = "Start",
		.sd_allowed   = M0_BITS(SWUPH_FINI)
	},
	[SWUPH_FINI] = {
		.sd_flags       = M0_SDF_TERMINAL,
		.sd_name        = "Fini",
		.sd_allowed     = 0
	},
};

struct m0_sm_conf m0_sns_cm_sw_update_conf = {
	.scf_name      = "SNS sw update phases",
	.scf_nr_states = ARRAY_SIZE(sw_update_fom_phases),
	.scf_state     = sw_update_fom_phases
};

static int sw_update_fom_tick(struct m0_fom *fom)
{
	struct m0_reqh_service     *service;
        struct m0_cm               *cm;
        struct m0_sns_cm_sw_update *swu_fop;
	struct m0_cm_proxy         *cm_proxy;
	const char                 *ep;

	service = fom->fo_service;
	switch (m0_fom_phase(fom)) {
	case SWUPH_START:
		swu_fop = m0_fop_data(fom->fo_fop);
		cm = m0_cmsvc2cm(service);
		if (cm == NULL)
			return -EINVAL;
		M0_LOG(M0_DEBUG, "Rcvd from %s hi: [%lu] [%lu] [%lu] [%lu]",
		       swu_fop->swu_base.swu_cm_ep.ep,
		       swu_fop->swu_base.swu_sw.sw_hi.ai_hi.u_hi,
		       swu_fop->swu_base.swu_sw.sw_hi.ai_hi.u_lo,
		       swu_fop->swu_base.swu_sw.sw_hi.ai_lo.u_hi,
		       swu_fop->swu_base.swu_sw.sw_hi.ai_lo.u_lo);
		m0_cm_lock(cm);
		if (m0_cm_has_more_data(cm) || cm->cm_aggr_grps_out_nr > 0) {
			ep = swu_fop->swu_base.swu_cm_ep.ep;
			cm_proxy = m0_cm_proxy_locate(cm, ep);
			M0_ASSERT(cm_proxy != NULL);
			M0_LOG(M0_DEBUG, "proxy hi: [%lu] [%lu] [%lu] [%lu]",
			       cm_proxy->px_sw.sw_hi.ai_hi.u_hi,
			       cm_proxy->px_sw.sw_hi.ai_hi.u_lo,
			       cm_proxy->px_sw.sw_hi.ai_lo.u_hi,
			       cm_proxy->px_sw.sw_hi.ai_lo.u_lo);

			if (m0_cm_ag_id_cmp(&swu_fop->swu_base.swu_sw.sw_hi,
					    &cm_proxy->px_sw.sw_hi) > 0) {
				m0_cm_proxy_update(cm_proxy, &swu_fop->swu_base.swu_sw.sw_lo,
						   &swu_fop->swu_base.swu_sw.sw_hi);
			}
			M0_CNT_INC(cm->cm_ready_fops_recvd);
			M0_LOG(M0_DEBUG, "got ready fop from %s: %d out of %d",
					 swu_fop->swu_base.swu_cm_ep.ep,
					 (int)cm->cm_ready_fops_recvd,
					 (int)m0_cm_proxy_nr(cm));
			/* This check is for the READY phase completion only. */
			if (cm->cm_ready_fops_recvd == cm->cm_proxy_nr)
				cm->cm_ops->cmo_complete(cm);
		}
		m0_cm_unlock(cm);
		m0_fom_phase_set(fom, SWUPH_FINI);
		break;
	default:
		M0_IMPOSSIBLE("Invalid fop");
		return -EINVAL;
	}

	return M0_FSO_WAIT;
}

static void sw_update_fom_fini(struct m0_fom *fom)
{
	M0_PRE(fom != NULL);

	m0_fom_fini(fom);
	m0_free(fom);
}

static size_t sw_update_fom_home_locality(const struct m0_fom *fom)
{
	return m0_fop_opcode(fom->fo_fop);
}

static void sw_update_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

static const struct m0_fom_ops sw_update_fom_ops = {
	.fo_fini          = sw_update_fom_fini,
	.fo_tick          = sw_update_fom_tick,
	.fo_home_locality = sw_update_fom_home_locality,
	.fo_addb_init     = sw_update_fom_addb_init
};

static int sw_update_fom_create(struct m0_fop *fop, struct m0_fom **out,
				struct m0_reqh *reqh)
{
	struct m0_fom          *fom;

	M0_PRE(fop != NULL);
	M0_PRE(out != NULL);

	M0_ALLOC_PTR(fom);
	if (fom == NULL)
		return -ENOMEM;

	m0_fom_init(fom, &fop->f_type->ft_fom_type, &sw_update_fom_ops, fop,
		    NULL, reqh, fop->f_type->ft_fom_type.ft_rstype);

	*out = fom;
	return 0;
}

const struct m0_fom_type_ops m0_sns_cm_sw_update_fom_type_ops = {
	.fto_create = sw_update_fom_create,
};

#undef M0_TRACE_SUBSYSTEM

/** @} CMREADY */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
