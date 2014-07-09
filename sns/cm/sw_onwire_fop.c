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
 * Original creation date: 06/07/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM

#include "lib/memory.h"
#include "lib/string.h"

#include "fop/fop.h"
#include "rpc/rpc.h"
#include "rpc/rpc_opcodes.h"

#include "cm/cm.h"
#include "sns/sns_addb.h"
#include "sns/cm/sw_onwire_fop.h"
#include "sns/cm/sw_onwire_fop_xc.h"

/**
   @addtogroup SNSCMSW

   @{
 */

extern const struct m0_fom_type_ops m0_sns_cm_sw_onwire_fom_type_ops;
extern struct m0_sm_conf m0_sns_cm_sw_onwire_conf;

M0_INTERNAL void m0_sns_cm_sw_onwire_fop_init(struct m0_fop_type *ft,
					      enum M0_RPC_OPCODES op,
					      struct m0_cm_type *cmt)
{
        M0_FOP_TYPE_INIT(ft,
			 .name      = "sns cm sw update fop",
			 .opcode    = op,
			 .xt        = m0_sns_cm_sw_onwire_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_ONEWAY,
			 .fom_ops   = &m0_sns_cm_sw_onwire_fom_type_ops,
			 .sm        = &m0_sns_cm_sw_onwire_conf,
			 .svc_type  = &cmt->ct_stype);
}

M0_INTERNAL void m0_sns_cm_sw_onwire_fop_fini(struct m0_fop_type *ft)
{
	m0_fop_type_fini(ft);
}

M0_INTERNAL int
m0_sns_cm_sw_onwire_fop_setup(struct m0_cm *cm, struct m0_fop_type *ft,
			      struct m0_fop *fop,
			      void (*fop_release)(struct m0_ref *),
			      const char *local_ep, const struct m0_cm_sw *sw)
{
	struct m0_sns_cm_sw_onwire *swo_fop;
	int                         rc = 0;

	M0_PRE(cm != NULL && sw != NULL && local_ep != NULL);

	M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_sns_sw_update,
		     M0_ADDB_CTX_VEC(&m0_sns_mod_addb_ctx),
		     sw->sw_lo.ai_hi.u_hi, sw->sw_lo.ai_hi.u_lo,
		     sw->sw_lo.ai_lo.u_hi, sw->sw_lo.ai_lo.u_lo,
		     sw->sw_hi.ai_hi.u_hi, sw->sw_hi.ai_hi.u_lo,
		     sw->sw_hi.ai_lo.u_hi, sw->sw_hi.ai_lo.u_lo);


	m0_fop_init(fop, ft, NULL, fop_release);
	rc = m0_fop_data_alloc(fop);
        if (rc  != 0) {
		m0_fop_fini(fop);
                return rc;
	}
	swo_fop = m0_fop_data(fop);
	rc = m0_cm_sw_onwire_init(&swo_fop->swo_base, local_ep, sw);

	return rc;
}

#undef M0_TRACE_SUBSYSTEM

/** @} SNSCMSW */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
