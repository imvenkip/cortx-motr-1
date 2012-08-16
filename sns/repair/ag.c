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
 * Original creation date: 16/04/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/memory.h"

#include "sns/repair/ag.h"
#include "sns/repair/cp.h"

/**
  @addtogroup SNSRepairAG

  @{
*/

static struct c2_cm_cp *cp_alloc(struct c2_cm_aggr_group *ag,
				 struct c2_bufvec *buf)
{
	struct c2_sns_repair_cp *cp;

	C2_PRE(ag != NULL && buf != NULL);

	C2_ALLOC_PTR(cp);
	if (cp != NULL)
		c2_cm_cp_init(&cp->rc_cp, ag, &c2_sns_repair_cp_ops, buf);

	return &cp->rc_cp;
}

static const struct c2_cm_aggr_group_ops group_ops = {
	.cago_cp_alloc  = cp_alloc,
	.cago_get       = NULL,
	.cago_completed = NULL,
	.cago_cp_nr     = NULL
};


/** @} SNSRepairAG */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
