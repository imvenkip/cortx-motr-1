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

#include "sns/cm/cm.h"
#include "sns/cm/sns_cp_onwire.h"
#include "sns/cm/sns_cp_onwire_xc.h"

struct m0_fop_type m0_sns_repair_cpx_fopt;
struct m0_fop_type m0_sns_repair_cpx_reply_fopt;
extern struct m0_cm_type sns_repair_cmt;

M0_INTERNAL void m0_sns_cm_repair_cpx_init(void)
{
        m0_sns_cpx_init(&m0_sns_repair_cpx_fopt,
			M0_SNS_CM_REPAIR_CP_OPCODE,
			"SNS Repair copy packet", m0_sns_cpx_xc,
			M0_RPC_ITEM_TYPE_REQUEST |
			M0_RPC_ITEM_TYPE_MUTABO,
			&sns_repair_cmt);
	m0_sns_cpx_init(&m0_sns_repair_cpx_reply_fopt,
			M0_SNS_CM_REPAIR_CP_REP_OPCODE,
			"SNS Repair copy packet reply",
			m0_sns_cpx_reply_xc, M0_RPC_ITEM_TYPE_REPLY,
			&sns_repair_cmt);
}

M0_INTERNAL void m0_sns_cm_repair_cpx_fini(void)
{
        m0_sns_cpx_fini(&m0_sns_repair_cpx_fopt);
        m0_sns_cpx_fini(&m0_sns_repair_cpx_reply_fopt);
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
