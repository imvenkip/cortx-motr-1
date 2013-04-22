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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 02/15/2013
 */

#include "fop/fop.h"
#include "sns/cm/cp.h"
#include "sns/cm/sns_cp_onwire_xc.h"
#include "cm/cp_onwire_xc.h"
#include "rpc/rpc_opcodes.h"

struct m0_fop_type m0_sns_cpx_fopt;
struct m0_fop_type m0_sns_cpx_reply_fopt;
extern const struct m0_fom_type_ops cp_fom_type_ops;

M0_INTERNAL int m0_sns_cpx_init(void)
{
        m0_xc_sns_cp_onwire_init();
        return M0_FOP_TYPE_INIT(&m0_sns_cpx_fopt,
                                .name      = "SNS copy packet",
                                .opcode    = M0_SNS_CM_CP_OPCODE,
                                .xt        = m0_sns_cpx_xc,
                                .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
                                             M0_RPC_ITEM_TYPE_MUTABO,
                                .fom_ops   = &cp_fom_type_ops,
                                .sm        = &m0_generic_conf,
                                .svc_type  = m0_reqh_service_type_find(
                                                "sns_cm")) ?:
                M0_FOP_TYPE_INIT(&m0_sns_cpx_reply_fopt,
                                .name      = "SNS copy packet reply",
                                .opcode    = M0_SNS_CM_CP_REP_OPCODE,
                                .xt        = m0_sns_cpx_reply_xc,
                                .rpc_flags = M0_RPC_ITEM_TYPE_REPLY,
                                .svc_type  = m0_reqh_service_type_find(
                                                "sns_cm"));
}

M0_INTERNAL void m0_sns_cpx_fini(void)
{
        m0_fop_type_fini(&m0_sns_cpx_fopt);
        m0_fop_type_fini(&m0_sns_cpx_reply_fopt);
        m0_xc_sns_cp_onwire_fini();
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
