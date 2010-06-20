/* -*- C -*- */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "addb/addb.h"

/**
   @addtogroup addb

   @todo check for recursive invocations.

   @{
 */

int c2_addb_init(void)
{
	return 0;
}

void c2_addb_fini(void)
{
}

void c2_addb_ctx_init(struct c2_addb_ctx *ctx, const struct c2_addb_ctx_type *t,
		      struct c2_addb_ctx *parent)
{
	ctx->ac_type = t;
	ctx->ac_parent = parent;
}

void c2_addb_ctx_fini(struct c2_addb_ctx *ctx)
{
}

#include <stdio.h>

void c2_addb_add(struct c2_addb_dp *dp)
{
	printf("addb: ctx: %s/%p, loc: %s, ev: %s/%s, rc: %i\n",
	       dp->ad_ctx->ac_type->act_name, dp->ad_ctx,
	       dp->ad_loc->al_name,
	       dp->ad_ev->ae_ops->aeo_name,
	       dp->ad_ev->ae_name,
	       dp->ad_rc);
}

static int subst_int(struct c2_addb_dp *dp, int rc)
{
	dp->ad_rc = rc;
	return 0;
}

static int subst_void(struct c2_addb_dp *dp)
{
	return 0;
}

const struct c2_addb_ev_ops C2_ADDB_SYSCALL = {
	.aeo_subst = (c2_addb_ev_subst_t)subst_int,
	.aeo_size  = sizeof(int32_t),
	.aeo_name  = "syscall-failure"
};

const struct c2_addb_ev_ops C2_ADDB_CALL = {
	.aeo_subst = (c2_addb_ev_subst_t)subst_int,
	.aeo_size  = sizeof(int32_t),
	.aeo_name  = "call-failure"
};

const struct c2_addb_ev_ops C2_ADDB_STAMP = {
	.aeo_subst = (c2_addb_ev_subst_t)subst_void,
	.aeo_size  = 0,
	.aeo_name  = "."
};

const struct c2_addb_ev_ops C2_ADDB_FLAG = {
	.aeo_subst = (c2_addb_ev_subst_t)subst_void,
	.aeo_size  = sizeof(bool),
	.aeo_name  = "flag"
};

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
