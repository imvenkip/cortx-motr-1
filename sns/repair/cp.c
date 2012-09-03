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
#include "lib/memory.h" /* c2_free() */

#include "cm/ag.h"
#include "sns/repair/cp.h"
#include "sns/repair/cm.h"

/**
  @addtogroup SNSRepairCP

  @{
*/

static bool cp_invariant(const struct c2_cm_cp *cp)
{
	return true;
}

static int cp_init(struct c2_cm_cp *cp)
{
	struct c2_sns_repair_cm *rcm;

	C2_PRE(c2_fom_phase(&cp->c_fom) == C2_CCP_INIT);
	rcm = cm2sns(cp->c_ag->cag_cm);
	return C2_FSO_AGAIN;
}

static int cp_fini(struct c2_cm_cp *cp)
{
	struct c2_sns_repair_cp	*rcp;

	rcp = container_of(cp, struct c2_sns_repair_cp, rc_base);
	/*@todo Release data buffer to buffer pool.*/
	/* finailise data members.*/
	c2_cm_cp_fini(cp);
	/* Free copy packet.*/
	c2_free(rcp);
	return C2_FSO_AGAIN;
}

static int cp_read(struct c2_cm_cp *cp)
{
        return 0;
}

static int cp_write(struct c2_cm_cp *cp)
{
        return 0;
}

static int cp_send(struct c2_cm_cp *cp)
{
        return 0;
}

static int cp_recv(struct c2_cm_cp *cp)
{
        return 0;
}

int cp_xform(struct c2_cm_cp *cp)
{
        return 0;
}

static int cp_phase_next(struct c2_cm_cp *cp)
{
	return 0;
}

static void cp_complete(struct c2_cm_cp *cp)
{
}

static int cp_tick(struct c2_cm_cp *cp)
{
	return 0;
}

const struct c2_cm_cp_ops c2_sns_repair_cp_ops = {
	.co_action = {
		[C2_CCP_INIT]  = &cp_init,
		[C2_CCP_READ]  = &cp_read,
		[C2_CCP_WRITE] = &cp_write,
		[C2_CCP_XFORM] = &cp_xform,
		[C2_CCP_SEND]  = &cp_send,
		[C2_CCP_RECV]  = &cp_recv,
		[C2_CCP_FINI]  = &cp_fini
	},
	.co_complete	       = &cp_complete,
	.co_phase_next	       = &cp_phase_next,
	.co_invariant	       = &cp_invariant,
	.co_tick	       = &cp_tick,
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

