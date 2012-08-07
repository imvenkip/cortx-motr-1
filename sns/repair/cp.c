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
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/trace.h"
#include "lib/misc.h"

#include "cm/cm.h"
#include "reqh/reqh.h"
#include "lib/finject.h"

/**
  @addtogroup snsrepair
  @{
*/

int c2_sns_repair_cp_alloc(struct c2_cm *cm, struct c2_cm_cp **cp)
{
	return 0;
}

void c2_sns_repair_cp_release(struct c2_cm_cp *cp)
{
}

static const struct c2_cm_cp_ops c2_sns_repair_cp_ops = {
        .cpo_alloc     = &c2_sns_repair_cp_alloc,
        .cpo_release   = &c2_sns_repair_cp_release,
        .cpo_encode    = &c2_cm_cp_default_encode,
        .cpo_decode    = &c2_cm_cp_default_decode,
        .cpo_complete  = &c2_cm_cp_default_complete
};

void c2_sns_repair_cp_init(struct c2_sns_repair_cp *scp,
			   const struct c2_fid *gfid,
			   const struct c2_ext *gext)
{
        C2_PRE(gfid != NULL);
        C2_PRE(gext != NULL);
        C2_PRE(c2_ext_length(gext) > 0);

	c2_cm_cp_init(&scp->rc_base);
	scp->rc_base.cp_ops = &c2_sns_repair_cp_ops;
        scp->rc_gfid  = *gfid;
        scp->rc_gext  = *gext;
        C2_SET0(&scp->rc_cfid);
        C2_SET0(&scp->rc_cext);
}

void c2_sns_repair_cp_fini(struct c2_sns_repair_cp *scp)
{
        C2_SET0(&scp->rc_cfid);
        C2_SET0(&scp->rc_cext);
	c2_cm_cp_fini(&scp->rc_base);
}

struct c2_sns_repair_cp *c2_sns_repair_cp_get(struct c2_cm *cm)
{
	struct c2_sns_repair_cp *cp;
	/**
	 * @todo Allocation for SNS copy packet from buffer pool.
	 */
	C2_ALLOC_PTR(cp);
	return cp;
}

/** @} snsrepair */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
