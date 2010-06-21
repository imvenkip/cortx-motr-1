/* -*- C -*- */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/arith.h"

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
	enum c2_addb_ev_level     lev;
	struct c2_addb_ctx       *ctx;
	const struct c2_addb_ev  *ev;

	ctx = dp->ad_ctx;
	ev  = dp->ad_ev;
	lev = max_check(dp->ad_level, max_check(ev->ae_level,
						ev->ae_ops->aeo_level));
	if (lev >= AEL_NOTE) {
		printf("addb: ctx: %s/%p, loc: %s, ev: %s/%s, rc: %i\n",
		       ctx->ac_type->act_name, ctx,
		       dp->ad_loc->al_name,
		       ev->ae_ops->aeo_name,
		       ev->ae_name,
		       dp->ad_rc);
	}
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

static int subst_uint64_t(struct c2_addb_dp *dp, uint64_t val)
{
	dp->ad_rc = val;
	return 0;
}

const struct c2_addb_ev_ops C2_ADDB_SYSCALL = {
	.aeo_subst = (c2_addb_ev_subst_t)subst_int,
	.aeo_size  = sizeof(int32_t),
	.aeo_name  = "syscall-failure",
	.aeo_level = AEL_NOTE
};

const struct c2_addb_ev_ops C2_ADDB_CALL = {
	.aeo_subst = (c2_addb_ev_subst_t)subst_int,
	.aeo_size  = sizeof(int32_t),
	.aeo_name  = "call-failure",
	.aeo_level = AEL_NOTE
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

const struct c2_addb_ev_ops C2_ADDB_INVAL = {
	.aeo_subst = (c2_addb_ev_subst_t)subst_uint64_t,
	.aeo_size  = sizeof(uint64_t),
	.aeo_name  = "inval"
};

struct c2_addb_ev c2_addb_oom = {
	.ae_name = "oom",
	.ae_id   = 0x3,
	.ae_ops  = &C2_ADDB_STAMP
};

static const struct c2_addb_ctx_type c2_addb_global_ctx_type = {
	.act_name = "global"
};

struct c2_addb_ctx c2_addb_global_ctx = {
	.ac_type   = &c2_addb_global_ctx_type,
	.ac_parent = NULL
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
