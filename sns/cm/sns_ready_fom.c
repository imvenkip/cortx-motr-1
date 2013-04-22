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
#include "sns/cm/sns_ready_fop.h"
#include "sns/cm/sns_ready_fom.h"

/**
   @addtogroup CMREADY

   @{
 */

static struct m0_sm_state_descr sns_ready_phases[] = {
	[RPH_START] = {
		.sd_flags     = M0_SDF_INITIAL,
		.sd_name      = "Start",
		.sd_allowed   = M0_BITS(RPH_FINI)
	},
	[RPH_FINI] = {
		.sd_flags       = M0_SDF_TERMINAL,
		.sd_name        = "Fini",
		.sd_allowed     = 0
	},
};

struct m0_sm_conf m0_sns_cm_ready_conf = {
	.scf_name      = "SNS Ready phases",
	.scf_nr_states = ARRAY_SIZE(sns_ready_phases),
	.scf_state     = sns_ready_phases
};

static int sns_ready_fom_tick(struct m0_fom *fom)
{
	struct m0_reqh_service *service;
        struct m0_cm           *cm;
        struct m0_sns_cm_ready *r_fop;
	struct m0_cm_proxy     *cm_proxy;

	service = fom->fo_service;
	switch (m0_fom_phase(fom)) {
	case RPH_START:
		r_fop = m0_fop_data(fom->fo_fop);
		cm = m0_cmsvc2cm(service);
		if (cm == NULL)
			return -EINVAL;
		m0_cm_lock(cm);
		cm_proxy = m0_cm_proxy_locate(cm, r_fop->scr_base.r_cm_ep.ep);
		m0_cm_proxy_update(cm_proxy, &r_fop->scr_base.r_sw.sw_lo,
				   &r_fop->scr_base.r_sw.sw_hi);
		M0_CNT_INC(cm->cm_ready_fops_recvd);
		if (cm->cm_ready_fops_recvd == m0_cm_proxy_nr(cm))
			cm->cm_ops->cmo_complete(cm);
		m0_cm_unlock(cm);
		m0_fom_phase_set(fom, RPH_FINI);
		break;
	default:
		M0_IMPOSSIBLE("Invalid fop");
		return -EINVAL;
	}

	return M0_FSO_WAIT;
}

static void sns_ready_fom_fini(struct m0_fom *fom)
{
	M0_PRE(fom != NULL);

	m0_fom_fini(fom);
	m0_free(fom);
}

static size_t sns_ready_fom_home_locality(const struct m0_fom *fom)
{
	return m0_fop_opcode(fom->fo_fop);
}

static void sns_ready_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

static const struct m0_fom_ops sns_ready_fom_ops = {
	.fo_fini          = sns_ready_fom_fini,
	.fo_tick          = sns_ready_fom_tick,
	.fo_home_locality = sns_ready_fom_home_locality,
	.fo_addb_init     = sns_ready_fom_addb_init
};

static int sns_ready_fom_create(struct m0_fop *fop, struct m0_fom **out,
				struct m0_reqh *reqh)
{
	struct m0_fom          *fom;

	M0_PRE(fop != NULL);
	M0_PRE(out != NULL);

	M0_ALLOC_PTR(fom);
	if (fom == NULL)
		return -ENOMEM;

	m0_fom_init(fom, &fop->f_type->ft_fom_type, &sns_ready_fom_ops, fop,
		    NULL, reqh, fop->f_type->ft_fom_type.ft_rstype);

	*out = fom;
	return 0;
}

const struct m0_fom_type_ops m0_sns_cm_ready_fom_type_ops = {
	.fto_create = sns_ready_fom_create,
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
