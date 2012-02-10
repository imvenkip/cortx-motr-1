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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 *                  Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 06/19/2010
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdarg.h>

#include "lib/arith.h" /* max_check */
#include "lib/memory.h"/*c2_alloc/c2_free */
#include "lib/cdefs.h" /* C2_EXPORTED */
#include "lib/errno.h" /* errno */
#include "lib/misc.h"
#include "lib/rwlock.h"
#include "stob/stob.h"
#include "db/db.h"

#include "addb/addb.h"

/**
   @addtogroup addb

   @todo check for recursive invocations.

   @{
 */

/*
 * This can be changed.
 */
enum c2_addb_ev_level c2_addb_level_default = AEL_NOTE;
C2_EXPORTED(c2_addb_level_default);

/**
   ADDB record store type.

   This type is inited while system startup. For clients, we may configure it
   as network; while for servers, we may configure it to store record into stob.
   Along with this variable, corresponding parameter should be configured below.
*/
static enum c2_addb_rec_store_type c2_addb_store_type  = C2_ADDB_REC_STORE_NONE;
static struct c2_stob             *c2_addb_store_stob     = NULL;
static struct c2_dtx              *c2_addb_store_stob_tx  = NULL;
static struct c2_table            *c2_addb_store_table    = NULL;
static struct c2_dbenv            *c2_addb_store_db_env   = NULL;
static struct c2_net_conn         *c2_addb_store_net_conn = NULL;

static c2_addb_stob_add_t c2_addb_stob_add_p = NULL;
static c2_addb_db_add_t   c2_addb_db_add_p   = NULL;
static c2_addb_net_add_t  c2_addb_net_add_p  = NULL;

enum {
	ADDB_CUSTOM_MSG_SIZE = 256,
};

int c2_addb_init(void)
{
	return 0;
}
C2_EXPORTED(c2_addb_init);

void c2_addb_fini(void)
{
}
C2_EXPORTED(c2_addb_fini);

/**
   Choose default addb event level, return the original level.
*/
enum c2_addb_ev_level c2_addb_choose_default_level(enum c2_addb_ev_level level)
{
	enum c2_addb_ev_level orig = c2_addb_level_default;

	C2_ASSERT(AEL_NONE <= level && level <= AEL_MAX);

	c2_addb_level_default = level;
	return orig;
}
C2_EXPORTED(c2_addb_choose_default_level);

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
		c2_addb_stob_add_p(dp, c2_addb_store_stob_tx,
				   c2_addb_store_stob);
		break;
	case C2_ADDB_REC_STORE_DB:
		C2_ASSERT(c2_addb_store_table != NULL);
		C2_ASSERT(c2_addb_db_add_p != NULL);
		c2_addb_db_add_p(dp, c2_addb_store_db_env, c2_addb_store_table);
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

void c2_addb_add_custom(struct c2_addb_ctx *ctx, const struct c2_addb_loc *loc,
			const char *fmt, ...)
{
	char msg[ADDB_CUSTOM_MSG_SIZE];
	va_list args;

	memset(msg, 0, sizeof msg);

	va_start(args, fmt);
	vsnprintf(msg, sizeof msg, fmt, args);
	va_end(args);

	C2_ADDB_ADD(ctx, loc, c2_addb_trace, msg);
}

static int subst_name_int(struct c2_addb_dp *dp, const char *name, int rc)
{
	dp->ad_name = name;
	dp->ad_rc = rc;
	return 0;
}

static int subst_name(struct c2_addb_dp *dp, const char *name)
{
	dp->ad_name = name;
	dp->ad_rc = 0;
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

extern int c2_addb_trace_getsize(struct c2_addb_dp *dp);

extern int c2_addb_trace_pack(struct c2_addb_dp *dp,
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
	XXX disabled to avoid recursion. These ops are used by events which are
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

const struct c2_addb_ev_ops C2_ADDB_TRACE = {
	.aeo_subst   = (c2_addb_ev_subst_t)subst_name,
	.aeo_pack    = c2_addb_trace_pack,
	.aeo_getsize = c2_addb_trace_getsize,
	.aeo_size    = sizeof(char *),
	.aeo_name    = "trace",
	.aeo_level   = AEL_WARN
};
C2_EXPORTED(C2_ADDB_TRACE);

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

struct c2_addb_ev c2_addb_trace = {
	.ae_name = "trace",
	.ae_id   = 0x30,
	.ae_ops  = &C2_ADDB_TRACE,
};
C2_EXPORTED(c2_addb_trace);

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
	c2_addb_store_stob_tx  = NULL;
	c2_addb_store_table    = NULL;
	c2_addb_store_db_env   = NULL;
	c2_addb_store_net_conn = NULL;

	c2_addb_stob_add_p = NULL;
	c2_addb_db_add_p   = NULL;
	c2_addb_net_add_p  = NULL;

        va_start(varargs, type);

	switch (type) {
	case C2_ADDB_REC_STORE_STOB:
		c2_addb_store_type    = C2_ADDB_REC_STORE_STOB;
		c2_addb_stob_add_p    = va_arg(varargs, c2_addb_stob_add_t);
		c2_addb_store_stob    = va_arg(varargs, struct c2_stob*);
		c2_addb_store_stob_tx = va_arg(varargs, struct c2_dtx*);
		break;

	case C2_ADDB_REC_STORE_DB:
		c2_addb_store_type   = C2_ADDB_REC_STORE_DB;
		c2_addb_db_add_p     = va_arg(varargs, c2_addb_db_add_t);
		c2_addb_store_table  = va_arg(varargs, struct c2_table*);
		c2_addb_store_db_env = va_arg(varargs, struct c2_dbenv*);
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
