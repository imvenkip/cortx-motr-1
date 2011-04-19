/* -*- C -*- */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/arith.h" /* max_check */
#include "lib/memory.h"/*c2_alloc/c2_free */
#include "lib/cdefs.h" /* C2_EXPORTED */
#include "lib/errno.h" /* errno */
#include "lib/misc.h"
#include "lib/rwlock.h"
#include "stob/stob.h"
#include "db/db.h"
#include "net/net.h"

#include "addb/addb.h"

/**
   @addtogroup addb

   @todo check for recursive invocations.

   @{
 */

/*
 * This can be changed.
 */
int c2_addb_level_default = AEL_NOTE;
C2_EXPORTED(c2_addb_level_default);


/**
   ADDB record store type.

   This type is inited while system startup. For clients, we may configure it
   as network; while for servers, we may configure it to store record into stob.
   Along with this variable, corresponding parameter should be configured below.
*/
static enum c2_addb_rec_store_type c2_addb_store_type  = C2_ADDB_REC_STORE_NONE;
static struct c2_stob             *c2_addb_store_stob  = NULL;
static struct c2_table            *c2_addb_store_table = NULL;
static struct c2_db_tx            *c2_addb_store_tx    = NULL;
static struct c2_net_conn         *c2_addb_store_net_conn = NULL;

static c2_addb_stob_add_t c2_addb_stob_add_p = NULL;
static c2_addb_db_add_t   c2_addb_db_add_p   = NULL;
static c2_addb_net_add_t  c2_addb_net_add_p  = NULL;

int c2_addb_init(void)
{
	return 0;
}
C2_EXPORTED(c2_addb_init);

void c2_addb_fini(void)
{
}
C2_EXPORTED(c2_addb_fini);

void c2_addb_ctx_init(struct c2_addb_ctx *ctx, const struct c2_addb_ctx_type *t,
		      struct c2_addb_ctx *parent)
{
	ctx->ac_type = t;
	ctx->ac_parent = parent;
}
C2_EXPORTED(c2_addb_ctx_init);

void c2_addb_ctx_fini(struct c2_addb_ctx *ctx)
{
}
C2_EXPORTED(c2_addb_ctx_fini);

/* defined in {,linux_kernel/}addb_console.c */
void c2_addb_console(enum c2_addb_ev_level lev, struct c2_addb_dp *dp);

void c2_addb_add(struct c2_addb_dp *dp)
{
	enum c2_addb_ev_level     lev;
	const struct c2_addb_ev  *ev;

	ev  = dp->ad_ev;
	lev = max_check(dp->ad_level, max_check(ev->ae_level,
						ev->ae_ops->aeo_level));
	/* log high priority data points to the console */
	if (lev > AEL_NOTE)
		c2_addb_console(lev, dp);

	switch (c2_addb_store_type) {
	case C2_ADDB_REC_STORE_STOB:
		C2_ASSERT(c2_addb_store_stob != NULL);
		C2_ASSERT(c2_addb_stob_add_p != NULL);
		c2_addb_stob_add_p(dp, c2_addb_store_stob);
		break;
	case C2_ADDB_REC_STORE_DB:
		C2_ASSERT(c2_addb_store_table != NULL);
		C2_ASSERT(c2_addb_db_add_p != NULL);
		c2_addb_db_add_p(dp, c2_addb_store_tx, c2_addb_store_table);
		break;
	case C2_ADDB_REC_STORE_NETWORK:
		C2_ASSERT(c2_addb_store_net_conn != NULL);
		C2_ASSERT(c2_addb_net_add_p != NULL);
		c2_addb_net_add_p(dp, c2_addb_store_net_conn);
		break;
	default:
		C2_ASSERT(c2_addb_store_type == C2_ADDB_REC_STORE_NONE);
		break;
	}
}
C2_EXPORTED(c2_addb_add);

static int subst_name_int(struct c2_addb_dp *dp, const char *name, int rc)
{
	dp->ad_name = name;
	dp->ad_rc = rc;
	return 0;
}

static int subst_int(struct c2_addb_dp *dp, int rc)
{
	dp->ad_name = "";
	dp->ad_rc = rc;
	return 0;
}

static int subst_void(struct c2_addb_dp *dp)
{
	dp->ad_name = "";
	dp->ad_rc = 0;
	return 0;
}

static int subst_uint64_t(struct c2_addb_dp *dp, uint64_t val)
{
	dp->ad_name = "";
	dp->ad_rc = val;
	return 0;
}

const struct c2_addb_ev_ops C2_ADDB_SYSCALL = {
	.aeo_subst = (c2_addb_ev_subst_t)subst_int,
	.aeo_size  = sizeof(int32_t),
	.aeo_name  = "syscall-failure",
	.aeo_level = AEL_NOTE
};
C2_EXPORTED(C2_ADDB_SYSCALL);

/** get size for data point opaque data */
extern int c2_addb_func_fail_getsize(struct c2_addb_dp *dp);

extern int c2_addb_func_fail_pack(struct c2_addb_dp *dp,
				  struct c2_addb_record *rec);

extern int c2_addb_call_getsize(struct c2_addb_dp *dp);
extern int c2_addb_call_pack(struct c2_addb_dp *dp,
			     struct c2_addb_record *rec);

extern int c2_addb_flag_getsize(struct c2_addb_dp *dp);
extern int c2_addb_flag_pack(struct c2_addb_dp *dp,
			     struct c2_addb_record *rec);

extern int c2_addb_inval_getsize(struct c2_addb_dp *dp);
extern int c2_addb_inval_pack(struct c2_addb_dp *dp,
			      struct c2_addb_record *rec);

extern int c2_addb_empty_getsize(struct c2_addb_dp *dp);
extern int c2_addb_empty_pack(struct c2_addb_dp *dp,
			      struct c2_addb_record *rec);


const struct c2_addb_ev_ops C2_ADDB_FUNC_CALL = {
	.aeo_subst   = (c2_addb_ev_subst_t)subst_name_int,
	.aeo_pack    = c2_addb_func_fail_pack,
	.aeo_getsize = c2_addb_func_fail_getsize,
	.aeo_size    = sizeof(int32_t) + sizeof(char *),
	.aeo_name    = "function-failure",
	.aeo_level   = AEL_NOTE
};
C2_EXPORTED(C2_ADDB_FUNC_CALL);

const struct c2_addb_ev_ops C2_ADDB_CALL = {
	.aeo_subst   = (c2_addb_ev_subst_t)subst_int,
	.aeo_pack    = c2_addb_call_pack,
	.aeo_getsize = c2_addb_call_getsize,
	.aeo_size    = sizeof(int32_t),
	.aeo_name    = "call-failure",
	.aeo_level   = AEL_NOTE
};
C2_EXPORTED(C2_ADDB_CALL);

const struct c2_addb_ev_ops C2_ADDB_STAMP = {
	.aeo_subst   = (c2_addb_ev_subst_t)subst_void,
/*
	XXX disabled to aviod recursion. These ops are used by events which are
            defined and generated in network/rpc layer.
*/
/*	.aeo_pack    = c2_addb_empty_pack,
	.aeo_getsize = c2_addb_empty_getsize,
*/
	.aeo_size    = 0,
	.aeo_name    = "."
};
C2_EXPORTED(C2_ADDB_STAMP);

const struct c2_addb_ev_ops C2_ADDB_FLAG = {
	.aeo_subst   = (c2_addb_ev_subst_t)subst_void,
	.aeo_pack    = c2_addb_flag_pack,
	.aeo_getsize = c2_addb_flag_getsize,
	.aeo_size    = sizeof(bool),
	.aeo_name    = "flag"
};
C2_EXPORTED(C2_ADDB_FLAG);

const struct c2_addb_ev_ops C2_ADDB_INVAL = {
	.aeo_subst   = (c2_addb_ev_subst_t)subst_uint64_t,
	.aeo_pack    = c2_addb_inval_pack,
	.aeo_getsize = c2_addb_inval_getsize,
	.aeo_size    = sizeof(uint64_t),
	.aeo_name    = "inval"
};
C2_EXPORTED(C2_ADDB_INVAL);

struct c2_addb_ev c2_addb_oom = {
	.ae_name = "oom",
	.ae_id   = 0x3,
	.ae_ops  = &C2_ADDB_STAMP
};
C2_EXPORTED(c2_addb_oom);

struct c2_addb_ev c2_addb_func_fail = {
	.ae_name = "func-fail",
	.ae_id   = 0x4,
	.ae_ops  = &C2_ADDB_FUNC_CALL
};
C2_EXPORTED(c2_addb_func_fail);

static const struct c2_addb_ctx_type c2_addb_global_ctx_type = {
	.act_name = "global"
};
C2_EXPORTED(c2_addb_global_ctx_type);

struct c2_addb_ctx c2_addb_global_ctx = {
	.ac_type   = &c2_addb_global_ctx_type,
	.ac_parent = NULL
};
C2_EXPORTED(c2_addb_global_ctx);

int c2_addb_choose_store_media(enum c2_addb_rec_store_type type, ...)
{
	va_list varargs;

	c2_addb_store_type     = C2_ADDB_REC_STORE_NONE;
	c2_addb_store_stob     = NULL;
	c2_addb_store_table    = NULL;
	c2_addb_store_tx       = NULL;
	c2_addb_store_net_conn = NULL;

	c2_addb_stob_add_p = NULL;
	c2_addb_db_add_p   = NULL;
	c2_addb_net_add_p  = NULL;

        va_start(varargs, type);

	switch (type) {
	case C2_ADDB_REC_STORE_STOB:
		c2_addb_store_type = C2_ADDB_REC_STORE_STOB;
		c2_addb_stob_add_p = va_arg(varargs, c2_addb_stob_add_t);
		c2_addb_store_stob = va_arg(varargs, struct c2_stob*);
		break;

	case C2_ADDB_REC_STORE_DB:
		c2_addb_store_type  = C2_ADDB_REC_STORE_DB;
		c2_addb_db_add_p    = va_arg(varargs, c2_addb_db_add_t);
		c2_addb_store_table = va_arg(varargs, struct c2_table*);
		c2_addb_store_tx    = va_arg(varargs, struct c2_db_tx*);
		break;

	case C2_ADDB_REC_STORE_NETWORK:
		c2_addb_store_type     = C2_ADDB_REC_STORE_NETWORK;
		c2_addb_net_add_p      = va_arg(varargs, c2_addb_net_add_t);
		c2_addb_store_net_conn = va_arg(varargs, struct c2_net_conn*);
		break;
	default:
		C2_ASSERT(c2_addb_store_type == C2_ADDB_REC_STORE_NONE);
	}

        va_end(varargs);
	return 0;
}
C2_EXPORTED(c2_addb_choose_store_media);
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
