/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/09/2010
 */

#include <linux/kernel.h> /* printk */

#include "addb/addb.h"

/**
   @addtogroup addb

   <b>Kernel space console log of addb messages.</b>

   @{
 */

M0_INTERNAL void m0_addb_console(enum m0_addb_ev_level lev,
				 struct m0_addb_dp *dp)
{
	struct m0_addb_ctx       *ctx;
	const struct m0_addb_ev  *ev;

	ctx = dp->ad_ctx;
	ev  = dp->ad_ev;
	/* XXX select KERN_ based on lev */
	printk(KERN_ERR "addb: ctx: %s/%p, loc: %s, ev: %s/%s, "
	       "rc: %i name: %s\n",
	       ctx->ac_type->act_name, ctx, dp->ad_loc->al_name,
	       ev->ae_ops->aeo_name, ev->ae_name, (int)dp->ad_rc, dp->ad_name);
}

/** @} end of addb group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
