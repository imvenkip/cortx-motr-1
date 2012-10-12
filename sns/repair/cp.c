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
 *                  Mandar Sawant <mandar_sawant@xyratex.com>
 *                  Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 08/06/2012
 */

#ifndef C2_TRACE_SUBSYSTEM
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_SNSREPAIR
#endif
#include "lib/memory.h" /* c2_free() */

#include "fop/fom.h"
#include "cm/ag.h"
#include "sns/repair/cp.h"
#include "sns/repair/cm.h"
#include "sns/repair/ag.h"

/**
  @addtogroup SNSRepairCP

  @{
*/

struct c2_sns_repair_cp *cp2snscp(const struct c2_cm_cp *cp)
{
	return container_of(cp, struct c2_sns_repair_cp, rc_base);
}

static bool cp_invariant(const struct c2_cm_cp *cp)
{
	struct c2_sns_repair_cp *sns_cp = cp2snscp(cp);

	return c2_fom_phase(&cp->c_fom) < SRP_NR &&
	       ergo(c2_fom_phase(&cp->c_fom) > C2_CCP_INIT,
		    c2_stob_id_is_set(&sns_cp->rc_sid));
}

/*
 * Uses GOB fid key and parity group number to generate a scalar to
 * help select a request handler locality for copy packet FOM.
 */
uint64_t cp_home_loc_helper(const struct c2_cm_cp *cp)
{
	struct c2_cm_ag_id *id;

	id = &cp->c_ag->cag_id;
	/* GOB.f_key + parity group number. */
	return id->ai_hi.u_lo + id->ai_lo.u_lo;
}

static int cp_init(struct c2_cm_cp *cp)
{
	C2_PRE(c2_fom_phase(&cp->c_fom) == C2_CCP_INIT);
	return cp->c_ops->co_phase_next(cp);
}

static int cp_fini(struct c2_cm_cp *cp)
{
	struct c2_sns_repair_cp	*rcp;

	rcp = cp2snscp(cp);
	/*
	 * XXX TODO: Release copy packet resource, e.g. data buffer and create
	 * new copy packets.
	 */

	return C2_FSO_WAIT;
}

static int cp_read(struct c2_cm_cp *cp)
{
	return cp->c_ops->co_phase_next(cp);
}

static int cp_write(struct c2_cm_cp *cp)
{
	return cp->c_ops->co_phase_next(cp);
}

static int cp_send(struct c2_cm_cp *cp)
{
	return C2_FSO_AGAIN;
}

static int cp_recv(struct c2_cm_cp *cp)
{
	return C2_FSO_AGAIN;
}

static int cp_phase_next(struct c2_cm_cp *cp)
{
	switch (c2_fom_phase(&cp->c_fom)) {
	case C2_CCP_INIT:
		c2_fom_phase_set(&cp->c_fom, C2_CCP_READ);
		break;
	case C2_CCP_READ:
		c2_fom_phase_set(&cp->c_fom, C2_CCP_XFORM);
		break;
	case C2_CCP_XFORM:
		c2_fom_phase_set(&cp->c_fom, C2_CCP_WRITE);
		break;
	case C2_CCP_WRITE:
		c2_fom_phase_set(&cp->c_fom, C2_CCP_FINI);
		return C2_FSO_WAIT;
	}
        return C2_FSO_AGAIN;
}

static void cp_complete(struct c2_cm_cp *cp)
{
}

static int cp_io_wait(struct c2_cm_cp *cp)
{
	return 0;
}

static void cp_free(struct c2_cm_cp *cp)
{
	C2_PRE(cp != NULL);

	c2_free(cp2snscp(cp));
}

const struct c2_cm_cp_ops c2_sns_repair_cp_ops = {
	.co_action = {
		[C2_CCP_INIT]  = &cp_init,
		[C2_CCP_READ]  = &cp_read,
		[C2_CCP_WRITE] = &cp_write,
		[C2_CCP_XFORM] = &c2_repair_cp_xform,
		[C2_CCP_SEND]  = &cp_send,
		[C2_CCP_RECV]  = &cp_recv,
		[C2_CCP_FINI]  = &cp_fini,
		[SRP_IO_WAIT]  = &cp_io_wait
	},
	.co_action_nr          = C2_CCP_NR,
	.co_phase_next	       = &cp_phase_next,
	.co_invariant	       = &cp_invariant,
	.co_home_loc_helper    = &cp_home_loc_helper,
	.co_complete	       = &cp_complete,
	.co_free               = &cp_free,
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
