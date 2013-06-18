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
#include "sns/cm/sw_onwire_fop.h"
#include "sns/cm/sw_onwire_fop_xc.h"

/**
   @addtogroup SNSCMSW

   @{
 */

struct m0_fop_type m0_sns_cm_sw_onwire_fopt;
extern const struct m0_fom_type_ops m0_sns_cm_sw_onwire_fom_type_ops;
extern struct m0_sm_conf m0_sns_cm_sw_onwire_conf;
extern struct m0_cm_type sns_cmt;

M0_INTERNAL int m0_sns_cm_sw_onwire_fop_init(void)
{
	m0_xc_sw_onwire_fop_init();
        return  M0_FOP_TYPE_INIT(&m0_sns_cm_sw_onwire_fopt,
                        .name      = "sns cm sw update fop",
                        .opcode    = M0_SNS_CM_SW_ONWIRE_FOP_OPCODE,
                        .xt        = m0_sns_cm_sw_onwire_xc,
                        .rpc_flags = M0_RPC_ITEM_TYPE_ONEWAY,
                        .fom_ops   = &m0_sns_cm_sw_onwire_fom_type_ops,
                        .sm        = &m0_sns_cm_sw_onwire_conf,
			.svc_type  = &sns_cmt.ct_stype);
}

M0_INTERNAL void m0_sns_cm_sw_onwire_fop_fini(void)
{
	m0_fop_type_fini(&m0_sns_cm_sw_onwire_fopt);
	m0_xc_sw_onwire_fop_fini();
}

M0_INTERNAL int
m0_sns_cm_sw_onwire_fop_setup(struct m0_cm *cm, struct m0_fop *fop,
			      void (*fop_release)(struct m0_ref *),
			      const char *local_ep, const struct m0_cm_sw *sw)
{
	struct m0_sns_cm_sw_onwire *swo_fop;
	int                         rc = 0;

	M0_PRE(cm != NULL && sw != NULL && local_ep != NULL);

	m0_fop_init(fop, &m0_sns_cm_sw_onwire_fopt, NULL, fop_release);
        rc = m0_fop_data_alloc(fop);
        if (rc  != 0) {
		m0_fop_fini(fop);
                return rc;
	}
	swo_fop = m0_fop_data(fop);
	rc = m0_cm_sw_onwire_init(&swo_fop->swo_base, local_ep, sw);
	if (rc != 0 )
		m0_fop_put(fop);
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
