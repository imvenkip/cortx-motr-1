/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 03/07/2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM

#include "lib/memory.h"
#include "lib/string.h"

#include "fop/fop.h"
#include "mero/setup.h" /* CS_MAX_EP_ADDR_LEN */
#include "rpc/rpc.h"
#include "rpc/rpc_opcodes.h"

#include "cm/cm.h"
#include "sns/cm/sw_update_fop.h"
#include "sns/cm/sw_update_fop_xc.h"

struct m0_fop_type m0_sns_cm_sw_update_fopt;
extern const struct m0_fom_type_ops m0_sns_cm_sw_update_fom_type_ops;
extern struct m0_sm_conf m0_sns_cm_sw_update_conf;
extern struct m0_cm_type sns_cmt;

M0_INTERNAL int m0_sns_cm_sw_update_fop_init(void)
{
	m0_xc_sw_update_fop_init();
        return  M0_FOP_TYPE_INIT(&m0_sns_cm_sw_update_fopt,
                        .name      = "sns cm sw update fop",
                        .opcode    = M0_SNS_CM_SW_UPDATE_FOP_OPCODE,
                        .xt        = m0_sns_cm_sw_update_xc,
                        .rpc_flags = M0_RPC_ITEM_TYPE_ONEWAY,
                        .fom_ops   = &m0_sns_cm_sw_update_fom_type_ops,
                        .sm        = &m0_sns_cm_sw_update_conf,
			.svc_type  = &sns_cmt.ct_stype);
}

M0_INTERNAL void m0_sns_cm_sw_update_fop_fini(void)
{
	m0_fop_type_fini(&m0_sns_cm_sw_update_fopt);
	m0_xc_sw_update_fop_fini();
}

M0_INTERNAL struct m0_fop *
m0_sns_cm_sw_update_fop_alloc(struct m0_cm *cm,
			      const struct m0_cm_sw *sw,
			      const char *local_ep)
{
	struct m0_fop              *fop;
	struct m0_sns_cm_sw_update *swu_fop;

	M0_PRE(cm != NULL && sw != NULL && local_ep != NULL);

	fop = m0_fop_alloc(&m0_sns_cm_sw_update_fopt, NULL);
	if (fop != NULL) {
		swu_fop = m0_fop_data(fop);
		m0_cm_sw_copy(&swu_fop->swu_base.swu_sw, sw);
		swu_fop->swu_base.swu_cm_ep.ep_size = CS_MAX_EP_ADDR_LEN;
		M0_ALLOC_ARR(swu_fop->swu_base.swu_cm_ep.ep,
				CS_MAX_EP_ADDR_LEN);
		if (swu_fop->swu_base.swu_cm_ep.ep == NULL ) {
			m0_fop_put(fop);
			return NULL;
		}
		strncpy(swu_fop->swu_base.swu_cm_ep.ep, local_ep,
				CS_MAX_EP_ADDR_LEN);
	}
	return fop;
}

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
