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
 * Original creation date: 08/06/2012
 */
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
	C2_PRE(c2_cm_cp_invariant(cp));

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
	struct c2_sns_repair_aggr_group *sns_ag;

	sns_ag = ag2snsag(cp->c_ag);

	return sns_ag->sag_id.rai_fid.f_key + sns_ag->sag_id.rai_pg_nr;
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

	rcp = cp2snscp(cp);
	/*@todo Release copy packet resource, e.g. data buffer. */
	return C2_FSO_WAIT;
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

static int cp_xform(struct c2_cm_cp *cp)
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

static int cp_io_wait(struct c2_cm_cp *cp)
{
	return 0;
}

static void cp_free(struct c2_cm_cp *cp)
{
	struct c2_sns_repair_cp	*rcp;

	rcp = cp2snscp(cp);
	c2_free(rcp);
}

const struct c2_cm_cp_ops c2_sns_repair_cp_ops = {
	.co_action = {
		[C2_CCP_INIT]  = &cp_init,
		[C2_CCP_READ]  = &cp_read,
		[C2_CCP_WRITE] = &cp_write,
		[C2_CCP_XFORM] = &cp_xform,
		[C2_CCP_SEND]  = &cp_send,
		[C2_CCP_RECV]  = &cp_recv,
		[C2_CCP_FINI]  = &cp_fini,
		[SRP_IO_WAIT]  = &cp_io_wait
	},
	.co_action_nr          = SRP_NR,
	.co_complete	       = &cp_complete,
	.co_phase_next	       = &cp_phase_next,
	.co_invariant	       = &cp_invariant,
	.co_home_loc_helper    = &cp_home_loc_helper,
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
