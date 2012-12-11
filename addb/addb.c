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

#include <stdarg.h>

#include "lib/arith.h"  /* max_check */
#include "lib/memory.h" /*m0_alloc/m0_free */
#include "lib/errno.h"  /* errno */
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
enum m0_addb_ev_level	m0_addb_level_default	      = AEL_NOTE;
enum m0_addb_ev_level	m0_addb_level_default_console = AEL_WARN;

/**
   ADDB record store type.

   This type is inited while system startup. For clients, we may configure it
   as network; while for servers, we may configure it to store record into stob.
   Along with this variable, corresponding parameter should be configured below.
*/
static enum m0_addb_rec_store_type m0_addb_store_type  = M0_ADDB_REC_STORE_NONE;
static struct m0_stob             *m0_addb_store_stob     = NULL;
static struct m0_dtx              *m0_addb_store_stob_tx  = NULL;
static struct m0_table            *m0_addb_store_table    = NULL;
static struct m0_dbenv            *m0_addb_store_db_env   = NULL;
static struct m0_net_conn         *m0_addb_store_net_conn = NULL;

static m0_addb_stob_add_t m0_addb_stob_add_p = NULL;
static m0_addb_db_add_t   m0_addb_db_add_p   = NULL;
static m0_addb_net_add_t  m0_addb_net_add_p  = NULL;

enum {
	ADDB_CUSTOM_MSG_SIZE = 256,
};

M0_INTERNAL int m0_addb_init(void)
{
	return 0;
}

M0_INTERNAL void m0_addb_fini(void)
{
}

/**
   Choose default addb event level, return the original level.
*/
enum m0_addb_ev_level m0_addb_choose_default_level(enum m0_addb_ev_level level)
{
	enum m0_addb_ev_level orig = m0_addb_level_default;

	M0_ASSERT(AEL_NONE <= level && level <= AEL_MAX);

	m0_addb_level_default = level;
	return orig;
}
M0_EXPORTED(m0_addb_choose_default_level);

/**
   Choose default addb event level for displaying output on the console,
   return the original level.
*/
M0_INTERNAL enum m0_addb_ev_level
m0_addb_choose_default_level_console(enum m0_addb_ev_level level)
{
	enum m0_addb_ev_level orig = m0_addb_level_default_console;

	M0_ASSERT(AEL_NONE <= level && level <= AEL_MAX);

	m0_addb_level_default_console = level;
	return orig;
}

M0_INTERNAL void m0_addb_ctx_init(struct m0_addb_ctx *ctx,
				  const struct m0_addb_ctx_type *t,
				  struct m0_addb_ctx *parent)
{
	ctx->ac_type = t;
	ctx->ac_parent = parent;
}

M0_INTERNAL void m0_addb_ctx_fini(struct m0_addb_ctx *ctx)
{
}

/* defined in {,linux_kernel/}addb_console.c */
M0_INTERNAL void m0_addb_console(enum m0_addb_ev_level lev,
				 struct m0_addb_dp *dp);

M0_INTERNAL void m0_addb_add(struct m0_addb_dp *dp)
{
	enum m0_addb_ev_level     lev;
	const struct m0_addb_ev  *ev;

	ev  = dp->ad_ev;
	lev = max_check(dp->ad_level, max_check(ev->ae_level,
						ev->ae_ops->aeo_level));
	/* log high priority data points to the console */
	if (lev > m0_addb_level_default_console)
		m0_addb_console(lev, dp);

	switch (m0_addb_store_type) {
	case M0_ADDB_REC_STORE_STOB:
		M0_ASSERT(m0_addb_store_stob != NULL);
		M0_ASSERT(m0_addb_stob_add_p != NULL);
		m0_addb_stob_add_p(dp, m0_addb_store_stob_tx,
				   m0_addb_store_stob);
		break;
	case M0_ADDB_REC_STORE_DB:
		M0_ASSERT(m0_addb_store_table != NULL);
		M0_ASSERT(m0_addb_db_add_p != NULL);
		m0_addb_db_add_p(dp, m0_addb_store_db_env, m0_addb_store_table);
		break;
	case M0_ADDB_REC_STORE_NETWORK:
		M0_ASSERT(m0_addb_store_net_conn != NULL);
		M0_ASSERT(m0_addb_net_add_p != NULL);
		m0_addb_net_add_p(dp, m0_addb_store_net_conn);
		break;
	default:
		M0_ASSERT(m0_addb_store_type == M0_ADDB_REC_STORE_NONE);
		break;
	}
}

static int subst_name_int(struct m0_addb_dp *dp, const char *name, int rc)
{
	dp->ad_name = name;
	dp->ad_rc = rc;
	return 0;
}

static int subst_name(struct m0_addb_dp *dp, const char *name)
{
	dp->ad_name = name;
	dp->ad_rc = 0;
	return 0;
}

static int subst_int(struct m0_addb_dp *dp, int rc)
{
	dp->ad_name = "";
	dp->ad_rc = rc;
	return 0;
}

static int subst_void(struct m0_addb_dp *dp)
{
	dp->ad_name = "";
	dp->ad_rc = 0;
	return 0;
}

static int subst_uint64_t(struct m0_addb_dp *dp, uint64_t val)
{
	dp->ad_name = "";
	dp->ad_rc = val;
	return 0;
}

const struct m0_addb_ev_ops M0_ADDB_SYSCALL = {
	.aeo_subst = (m0_addb_ev_subst_t)subst_int,
	.aeo_size  = sizeof(int32_t),
	.aeo_name  = "syscall-failure",
	.aeo_level = AEL_NOTE
};

/** get size for data point opaque data */
M0_INTERNAL int m0_addb_func_fail_getsize(struct m0_addb_dp *dp);

M0_INTERNAL int m0_addb_func_fail_pack(struct m0_addb_dp *dp,
				       struct m0_addb_record *rec);

M0_INTERNAL int m0_addb_call_getsize(struct m0_addb_dp *dp);
M0_INTERNAL int m0_addb_call_pack(struct m0_addb_dp *dp,
				  struct m0_addb_record *rec);

M0_INTERNAL int m0_addb_flag_getsize(struct m0_addb_dp *dp);
M0_INTERNAL int m0_addb_flag_pack(struct m0_addb_dp *dp,
				  struct m0_addb_record *rec);

M0_INTERNAL int m0_addb_inval_getsize(struct m0_addb_dp *dp);
M0_INTERNAL int m0_addb_inval_pack(struct m0_addb_dp *dp,
				   struct m0_addb_record *rec);

M0_INTERNAL int m0_addb_empty_getsize(struct m0_addb_dp *dp);
M0_INTERNAL int m0_addb_empty_pack(struct m0_addb_dp *dp,
				   struct m0_addb_record *rec);

M0_INTERNAL int m0_addb_trace_getsize(struct m0_addb_dp *dp);

M0_INTERNAL int m0_addb_trace_pack(struct m0_addb_dp *dp,
				   struct m0_addb_record *rec);

const struct m0_addb_ev_ops M0_ADDB_FUNC_CALL = {
	.aeo_subst   = (m0_addb_ev_subst_t)subst_name_int,
	.aeo_pack    = m0_addb_func_fail_pack,
	.aeo_getsize = m0_addb_func_fail_getsize,
	.aeo_size    = sizeof(int32_t) + sizeof(char *),
	.aeo_name    = "function-failure",
	.aeo_level   = AEL_NOTE
};

const struct m0_addb_ev_ops M0_ADDB_CALL = {
	.aeo_subst   = (m0_addb_ev_subst_t)subst_int,
	.aeo_pack    = m0_addb_call_pack,
	.aeo_getsize = m0_addb_call_getsize,
	.aeo_size    = sizeof(int32_t),
	.aeo_name    = "call-failure",
	.aeo_level   = AEL_NOTE
};

const struct m0_addb_ev_ops M0_ADDB_STAMP = {
	.aeo_subst   = (m0_addb_ev_subst_t)subst_void,
/*
	XXX disabled to avoid recursion. These ops are used by events which are
            defined and generated in network/rpc layer.
*/
/*	.aeo_pack    = m0_addb_empty_pack,
	.aeo_getsize = m0_addb_empty_getsize,
*/
	.aeo_size    = 0,
	.aeo_name    = "."
};

const struct m0_addb_ev_ops M0_ADDB_FLAG = {
	.aeo_subst   = (m0_addb_ev_subst_t)subst_void,
	.aeo_pack    = m0_addb_flag_pack,
	.aeo_getsize = m0_addb_flag_getsize,
	.aeo_size    = sizeof(bool),
	.aeo_name    = "flag"
};

const struct m0_addb_ev_ops M0_ADDB_INVAL = {
	.aeo_subst   = (m0_addb_ev_subst_t)subst_uint64_t,
	.aeo_pack    = m0_addb_inval_pack,
	.aeo_getsize = m0_addb_inval_getsize,
	.aeo_size    = sizeof(uint64_t),
	.aeo_name    = "inval"
};

const struct m0_addb_ev_ops M0_ADDB_TRACE = {
	.aeo_subst   = (m0_addb_ev_subst_t)subst_name,
	.aeo_pack    = m0_addb_trace_pack,
	.aeo_getsize = m0_addb_trace_getsize,
	.aeo_size    = sizeof(char *),
	.aeo_name    = "trace",
	.aeo_level   = AEL_TRACE
};

M0_ADDB_EV_DEFINE_PUBLIC(m0_addb_oom, "oom", M0_ADDB_EVENT_OOM, M0_ADDB_STAMP);

M0_ADDB_EV_DEFINE_PUBLIC(m0_addb_func_fail, "func-fail",		\
			 M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);

M0_ADDB_EV_DEFINE_PUBLIC(m0_addb_trace, "trace", M0_ADDB_EVENT_TRACE,	\
			 M0_ADDB_TRACE);


static const struct m0_addb_ctx_type m0_addb_global_ctx_type = {
	.act_name = "global"
};

struct m0_addb_ctx m0_addb_global_ctx = {
	.ac_type   = &m0_addb_global_ctx_type,
	.ac_parent = NULL
};

M0_INTERNAL int m0_addb_choose_store_media(enum m0_addb_rec_store_type type,
					   ...)
{
	va_list varargs;

	m0_addb_store_type     = M0_ADDB_REC_STORE_NONE;
	m0_addb_store_stob     = NULL;
	m0_addb_store_stob_tx  = NULL;
	m0_addb_store_table    = NULL;
	m0_addb_store_db_env   = NULL;
	m0_addb_store_net_conn = NULL;

	m0_addb_stob_add_p = NULL;
	m0_addb_db_add_p   = NULL;
	m0_addb_net_add_p  = NULL;

        va_start(varargs, type);

	switch (type) {
	case M0_ADDB_REC_STORE_STOB:
		m0_addb_store_type    = M0_ADDB_REC_STORE_STOB;
		m0_addb_stob_add_p    = va_arg(varargs, m0_addb_stob_add_t);
		m0_addb_store_stob    = va_arg(varargs, struct m0_stob*);
		m0_addb_store_stob_tx = va_arg(varargs, struct m0_dtx*);
		break;

	case M0_ADDB_REC_STORE_DB:
		m0_addb_store_type   = M0_ADDB_REC_STORE_DB;
		m0_addb_db_add_p     = va_arg(varargs, m0_addb_db_add_t);
		m0_addb_store_table  = va_arg(varargs, struct m0_table*);
		m0_addb_store_db_env = va_arg(varargs, struct m0_dbenv*);
		break;

	case M0_ADDB_REC_STORE_NETWORK:
		m0_addb_store_type     = M0_ADDB_REC_STORE_NETWORK;
		m0_addb_net_add_p      = va_arg(varargs, m0_addb_net_add_t);
		m0_addb_store_net_conn = va_arg(varargs, struct m0_net_conn*);
		break;
	default:
		M0_ASSERT(m0_addb_store_type == M0_ADDB_REC_STORE_NONE);
	}

        va_end(varargs);
	return 0;
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
