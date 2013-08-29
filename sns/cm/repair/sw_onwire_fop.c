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
 * Original creation date: 08/25/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM

#include "fop/fop.h"

#include "cm/cm.h"
#include "sns/sns_addb.h"
#include "sns/cm/sw_onwire_fop.h"

/**
   @addtogroup SNSCMSW

   @{
 */

struct m0_fop_type repair_sw_onwire_fopt;
extern struct m0_cm_type sns_repair_cmt;

M0_INTERNAL int m0_sns_cm_repair_sw_onwire_fop_init(void)
{
        return m0_sns_cm_sw_onwire_fop_init(&repair_sw_onwire_fopt,
					    M0_SNS_CM_REPAIR_SW_FOP_OPCODE,
					    &sns_repair_cmt);
}

M0_INTERNAL void m0_sns_cm_repair_sw_onwire_fop_fini(void)
{
	m0_sns_cm_sw_onwire_fop_fini(&repair_sw_onwire_fopt);
}

M0_INTERNAL int
m0_sns_cm_repair_sw_onwire_fop_setup(struct m0_cm *cm, struct m0_fop *fop,
				     void (*fop_release)(struct m0_ref *),
				     const char *local_ep, const struct m0_cm_sw *sw)
{
	return m0_sns_cm_sw_onwire_fop_setup(cm, &repair_sw_onwire_fopt, fop,
					     fop_release, local_ep, sw);
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
