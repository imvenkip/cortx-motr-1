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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 08/06/2012
 */

#include "lib/errno.h"
#include "lib/memory.h"

#include "sns/repair/cp.h"
#include "sns/repair/cm.h"

/**
  @addtogroup SNSRepairCP

  @{
*/

static int repair_cp_init(struct c2_cm_cp *cp)
{
	struct c2_sns_repair_cm *rcm;

	C2_PRE(cp->c_fom.fo_phase == CCP_INIT);
	rcm = container_of(cp->c_ag->cag_cm, struct c2_sns_repair_cm, rc_cm);
	return C2_FSO_AGAIN;
}

static void repair_cp_fini(struct c2_cm_cp *cp)
{
	struct c2_sns_repair_cp	*rcp;

	rcp = container_of(cp, struct c2_sns_repair_cp, rc_cp);
	/*@todo Release data buffer to buffer pool.*/
	/* finailise data members.*/
	c2_cm_cp_fini(cp);
	/* Free copy packet.*/
	c2_free(rcp);
}

static int repair_cp_read(struct c2_cm_cp *cp)
{
        return 0;
}

static int repair_cp_write(struct c2_cm_cp *cp)
{
        return 0;
}

static int repair_cp_send(struct c2_cm_cp *cp)
{
        return 0;
}

static int repair_cp_recv(struct c2_cm_cp *cp)
{
        return 0;
}

int repair_cp_xform(struct c2_cm_cp *cp)
{
        return 0;
}

static int repair_cp_phase(struct c2_cm_cp *cp)
{
	return 0;
}

static void repair_cp_complete(struct c2_cm_cp *cp)
{
}

const struct c2_cm_cp_ops c2_sns_repair_cp_ops = {
	.co_init     = &repair_cp_init,
	.co_fini     = &repair_cp_fini,
	.co_read     = &repair_cp_read,
	.co_write    = &repair_cp_write,
	.co_send     = &repair_cp_send,
	.co_recv     = &repair_cp_recv,
	.co_xform    = &repair_cp_xform,
	.co_phase    = &repair_cp_phase,
	.co_complete = &repair_cp_complete
};

/** @} SNSRepairCP */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */

