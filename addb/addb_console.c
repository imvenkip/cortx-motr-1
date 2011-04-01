/* -*- C -*- */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h> /* printf */

#include "addb/addb.h"

/**
   @addtogroup addb

   <b>User space console log of addb messages.</b>

   @{
 */

void c2_addb_console(enum c2_addb_ev_level lev, struct c2_addb_dp *dp)
{
	struct c2_addb_ctx       *ctx;
	const struct c2_addb_ev  *ev;

	ctx = dp->ad_ctx;
	ev  = dp->ad_ev;
	printf("addb: ctx: %s/%p, loc: %s, ev: %s/%s, rc: %i name: %s\n",
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
