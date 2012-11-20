/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 30-Jan-2012
 */

#include "conf/confc.h"
#include "conf/obj_ops.h"
#include "conf/preload.h"  /* c2_conf_parse */
#include "conf/buf_ext.h"  /* c2_buf_is_aimed */
#include "colibri/magic.h" /* C2_CONFC_MAGIC, C2_CONFC_CTX_MAGIC */
#include "rpc/rpc.h"      /* c2_rpc_post */
#include "lib/cdefs.h"     /* C2_HAS_TYPE */
#include "lib/arith.h"     /* C2_CNT_INC, C2_CNT_DEC */
#include "lib/misc.h"      /* C2_IN */
#include "lib/errno.h"     /* ENOMEM, EPROTO */
#include "lib/memory.h"    /* C2_ALLOC_ARR, c2_free */

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_CONF
#include "lib/trace.h"     /* C2_LOG */

/**
 * @page confc-lspec confc Internals
 *
 * - @ref confc-lspec-state
 *   - @ref confc-lspec-state-initial
 *   - @ref confc-lspec-state-check
 *   - @ref confc-lspec-state-wait-reply
 *   - @ref confc-lspec-state-wait-status
 *   - @ref confc-lspec-state-grow-cache
 *   - @ref confc-lspec-state-terminal
 *   - @ref confc-lspec-state-failure
 * - @ref confc-lspec-walk
 * - @ref confc-lspec-grow
 * - @ref confc-lspec-thread
 * - @ref confc_dlspec "Detailed Logical Specification"
 *
 * <hr> <!------------------------------------------------------------>
 * @section confc-lspec-state State Specification
 *
 * A state machine is embedded into c2_confc_ctx structure as its @ref
 * c2_confc_ctx::fc_mach "fc_mach" member.  c2_confc_ctx_init()
 * initialises the state machine and sets its state to S_INITIAL.
 *
 * @dot
 * digraph confc_ctx_states {
 *     node [fontsize=9];
 *     edge [fontsize=9];
 *     S_INITIAL  [style=filled, fillcolor=lightgrey];
 *     S_TERMINAL [style=filled, fillcolor=lightgrey, label="S_TERMINAL"];
 *     S_FAILURE  [style=filled, fillcolor=lightgrey, label="S_FAILURE"];
 *
 *     S_CHECK [label="{ check_st_in() }\nS_CHECK"];
 *     S_WAIT_REPLY [label="{ wait_reply_st_in() }\nS_WAIT_REPLY"];
 *     S_WAIT_STATUS;
 *     S_GROW_CACHE [label="{ grow_cache_st_in() }\nS_GROW_CACHE"];
 *
 *     S_INITIAL -> S_CHECK;
 *     S_CHECK -> S_TERMINAL [label=
 *   "path target is reached\nand it is C2_CS_READY"];
 *     S_CHECK -> S_FAILURE [label="error"];
 *     S_CHECK -> S_WAIT_REPLY [label=
 *   "C2_CS_MISSING object\nis reached\n{ set status to C2_CS_LOADING;\nsend request }"];
 *     S_WAIT_REPLY -> S_FAILURE [label="timeout\nor\nrc != 0"];
 *     S_WAIT_REPLY -> S_GROW_CACHE [label="response received,\nrc == 0"];
 *     S_GROW_CACHE -> S_FAILURE [label="error"];
 *     S_GROW_CACHE -> S_CHECK [label="done"];
 *     S_CHECK -> S_WAIT_STATUS [label=
 *   "C2_CS_LOADING object\nis reached\n{ c2_clink_add(&obj->co_chan) }"];
 *     S_WAIT_STATUS -> S_CHECK [label="\"status updated\" event"];
 * }
 * @enddot
 *
 * @subsection confc-lspec-state-initial S_INITIAL
 *
 * Summary: c2_confc_ctx has just been initialised.
 *
 * c2_confc_open() populates c2_confc_ctx::fc_path array (path_store())
 * and posts an AST to c2_confc::cc_group.
 *
 * @note  c2_sm_ast_post() signals group's clink. Current design of
 *        confc assumes that some thread will respond to this event by
 *        calling c2_sm_asts_run().
 *
 * When the AST, posted by c2_confc_open(), is run, it moves a state
 * machine (c2_confc_ctx::fc_mach) to S_CHECK state.
 *
 * @subsection confc-lspec-state-check S_CHECK
 *
 * Summary: Traversing the path, checking whether the requested
 * configuration object is accessible.
 *
 * When S_CHECK state is entered, check_st_in() callback is invoked.
 * It calls path_walk() and, depending on the value returned by this
 * call, moves the state machine to another state:
 *
@verbatim
+--------------------+-----------------+
| path_walk() result |   next state    |
+--------------------+-----------------+
|    C2_CS_READY     |  S_TERMINAL     |
|    C2_CS_MISSING   |  S_WAIT_REPLY   |
|    C2_CS_LOADING   |  S_WAIT_STATUS  |
|         < 0        |  S_FAILURE      |
+--------------------+-----------------+
@endverbatim
 *
 * The algorithm of path_walk() is described below (see @ref
 * confc-lspec-walk).
 *
 * @subsection confc-lspec-state-wait-reply S_WAIT_REPLY
 *
 * Summary: Waiting for confd's reply to arrive.
 *
 * When a state machine is about to enter S_WAIT_REPLY state,
 * wait_reply_st_in() callback is executed. This callback sends
 * configuration request (c2_confc_ctx::fc_req) to the confd, using
 * c2_rpc_post().
 *
 * A state machine remains in S_WAIT_REPLY state until a reply from
 * confd arrives. This event triggers on_replied() callback.  If
 * c2_rpc_item::ri_error is non-zero, on_replied() posts an AST that
 * will eventually move the state machine to S_FAILURE state.  If
 * ->ri_error is zero, on_replied() increments rpc item's reference
 * counter (c2_rpc_item_get()) and posts an AST, scheduling transition
 * to S_GROW_CACHE state.
 *
 * @subsection confc-lspec-state-wait-status S_WAIT_STATUS
 *
 * Summary: Waiting for an object to be filled by another
 * configuration request.
 *
 * A state machine in S_WAIT_STATUS state remains idle until the
 * channel (c2_conf_obj::co_chan) that c2_confc_ctx::fc_clink is
 * registered with is signaled.  Such an event triggers
 * on_object_updated() callback, which de-registers the clink and
 * posts an AST that will eventually move the state machine to S_CHECK
 * state.
 *
 * @note  Object's channel (c2_conf_obj::co_chan) is signaled
 *        (c2_chan_broadcast()) when
 *        -#
 *          object_enrich() completes loading of configuration data
 *          into this object and changes its status to C2_CS_READY
 *          (loading succeeded) or C2_CS_MISSING (loading failed);
 *        -#
 *           the object is closed and its number of references becomes
 *           zero.  (This case is not applicable to S_WAIT_STATUS
 *           state.)
 *
 * @subsection confc-lspec-state-grow-cache S_GROW_CACHE
 *
 * Summary: Applying configuration data contained in confd's reply.
 *
 * When a state machine is entering S_GROW_CACHE state,
 * grow_cache_st_in() callback is invoked.  If the error code
 * contained in confd's response (c2_conf_fetch_resp::fr_rc) is zero,
 * the callback calls cache_grow() function (see @ref
 * confc-lspec-grow below).  The callback "releases" rpc item by
 * calling c2_rpc_item_put().  If ->fr_rc == 0 and cache_grow()
 * succeeds, grow_cache_st_in() moves the state machine to S_CHECK
 * state, otherwise --- to S_FAILURE state.
 *
 * @subsection confc-lspec-state-terminal S_TERMINAL
 *
 * Summary: Configuration retrieval succeeded.
 *
 * @subsection confc-lspec-state-failure S_FAILURE
 *
 * Summary: Configuration retrieval failed.
 *
 * <hr> <!------------------------------------------------------------>
 * @section confc-lspec-walk Walking the DAG
 *
 * path_walk() begins with locking the confc cache (c2_confc::cc_lock);
 * it unlocks the cache before returning.
 *
 * The function "moves" along the DAG of cached configuration objects,
 * starting at c2_confc_ctx::fc_origin object and following
 * c2_confc_ctx::fc_path.  Next object is found by calling
 * c2_conf_obj_ops::coo_lookup() with current object and path
 * component as parameters.  The iteration continues until
 * ->coo_lookup() fails, or a stub is met, or the end of path is
 * reached.
 *
 * path_walk_complete() applies the results of path walking:
 * increments reference counter of C2_CS_READY object, fills
 * c2_conf_fetch request for C2_CS_MISSING object, or registers clink
 * with the channel of C2_CS_LOADING object.
 *
 * <hr> <!------------------------------------------------------------>
 * @section confc-lspec-grow Growing the cache
 *
 * cache_grow() locks the cache (c2_confc::cc_lock) and unlocks before
 * returning.  The function performs the following operations for
 * every object descriptor (confx_object, defined in conf/onwire.ff):
 *   -#
 *      Tries to find an object with the same identity (type and id)
 *      in the registry of cached objects (c2_confc::cc_registry),
 *      using c2_conf_reg_lookup().
 *   -#
 *      If cached object is found, it is "enriched" (see
 *      object_enrich() below).  Otherwise new object is added to the
 *      cache with cache_add().
 *
 * cache_add() performs the following operations:
 *   -# c2_conf_obj_create() --- allocates configuration object and
 *      initialises its fields;
 *   -# c2_conf_obj_fill() --- fills new object with configuration data
 *      contained in on-wire object descriptor (confx_object);
 *   -# c2_conf_reg_add() --- adds configuration object to the
 *      registry of cached objects.
 *
 * object_enrich() compares cached object with the descriptor received
 * from the confd.  If a discrepancy is found (!c2_conf_obj_match()),
 * the function reports it (C2_ADDB_ADD()) and returns an error code.
 *
 * If there is no discrepancy, and the cached object is a stub,
 * object_enrich()
 *   - fills the cached object with configuration data
 *     (c2_conf_obj_fill());
 *   - changes status of the cached object to C2_CS_READY or
 *     C2_CS_MISSING, depending on whether filling was successful;
 *   - signals object's channel.
 *
 * <hr> <!------------------------------------------------------------>
 * @section confc-lspec-thread Threading and Concurrency Model
 *
 * There are as many state machines in operation as there are
 * unfinished c2_confc_open*() requests.
 *
 * At most one state transition (c2_sm_state_descr::sd_in()) can be
 * running at any given time.  Synchronization of state transitions is
 * achieved by using c2_sm_group (c2_confc::cc_group).
 *
 * Modifications to c2_confc instance and confc cache are protected by
 * c2_confc::cc_lock mutex, aka confc lock.
 *
 * If a function needs both locks -- group lock and confc lock -- for
 * its operation, group lock must be acquired first. (Note, that the
 * "function" here cannot be something invoked from an AST callback,
 * because otherwise it would deadlock on the group mutex.)
 *
 * A user managing the state machine group (c2_confc::cc_group) is
 * responsible for making sure c2_sm_asts_run() is called when
 * c2_sm_group::s_clink is signaled.  See @ref sm (search for `"ast"
 * thread'.)
 */

/**
 * @defgroup confc_dlspec confc Internals
 *
 * @see @ref conf, @ref confc-lspec "Logical Specification of confc"
 *
 * @{
 */

/* ------------------------------------------------------------------
 * State definitions
 * ------------------------------------------------------------------ */

static int check_st_in(struct c2_sm *mach);      /* S_CHECK */
static int wait_reply_st_in(struct c2_sm *mach); /* S_WAIT_REPLY */
static int grow_cache_st_in(struct c2_sm *mach); /* S_GROW_CACHE */

static bool check_st_invariant(const struct c2_sm *mach);    /* S_CHECK */
static bool failure_st_invariant(const struct c2_sm *mach);  /* S_FAILURE */
static bool terminal_st_invariant(const struct c2_sm *mach); /* S_TERMINAL */

/** States of c2_confc_ctx::fc_mach. */
enum confc_ctx_state { S_INITIAL, S_CHECK, S_WAIT_REPLY, S_WAIT_STATUS,
		       S_GROW_CACHE, S_FAILURE, S_TERMINAL, S_NR };

static const struct c2_sm_state_descr confc_ctx_states[S_NR] = {
	[S_INITIAL] = {
		.sd_flags     = C2_SDF_INITIAL,
		.sd_name      = "S_INITIAL",
		.sd_in        = NULL,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = 1 << S_CHECK
	},
	[S_CHECK] = {
		.sd_flags     = 0,
		.sd_name      = "S_CHECK",
		.sd_in        = check_st_in,
		.sd_ex        = NULL,
		.sd_invariant = check_st_invariant,
		.sd_allowed   = 1 << S_WAIT_REPLY | 1 << S_WAIT_STATUS
			      | 1 << S_TERMINAL | 1 << S_FAILURE
	},
	[S_WAIT_REPLY] = {
		.sd_flags     = 0,
		.sd_name      = "S_WAIT_REPLY",
		.sd_in        = wait_reply_st_in,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = 1 << S_GROW_CACHE | 1 << S_FAILURE
	},
	[S_WAIT_STATUS] = {
		.sd_flags     = 0,
		.sd_name      = "S_WAIT_STATUS",
		.sd_in        = NULL,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = 1 << S_CHECK
	},
	[S_GROW_CACHE] = {
		.sd_flags     = 0,
		.sd_name      = "S_GROW_CACHE",
		.sd_in        = grow_cache_st_in,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = 1 << S_CHECK | 1 << S_FAILURE
	},
	[S_FAILURE] = {
		.sd_flags     = C2_SDF_FAILURE,
		.sd_name      = "S_FAILURE",
		.sd_in        = NULL,
		.sd_ex        = NULL,
		.sd_invariant = failure_st_invariant,
		.sd_allowed   = 0
	},
	[S_TERMINAL] = {
		.sd_flags     = C2_SDF_TERMINAL,
		.sd_name      = "S_TERMINAL",
		.sd_in        = NULL,
		.sd_ex        = NULL,
		.sd_invariant = terminal_st_invariant,
		.sd_allowed   = 0
	}
};

static const struct c2_sm_conf confc_ctx_states_conf = {
	.scf_name      = "states of c2_confc_ctx::fc_mach",
	.scf_nr_states = S_NR,
	.scf_state     = confc_ctx_states
};

/* ------------------------------------------------------------------
 * Bob types and invariants
 * ------------------------------------------------------------------ */

static bool _confc_check(const void *bob);
static bool _ctx_check(const void *bob);
static bool on_object_updated(struct c2_clink *link);

static const struct c2_bob_type confc_bob = {
	.bt_name         = "c2_confc",
	.bt_magix_offset = C2_MAGIX_OFFSET(struct c2_confc, cc_magic),
	.bt_magix        = C2_CONFC_MAGIC,
	.bt_check        = _confc_check
};
C2_BOB_DEFINE(static, &confc_bob, c2_confc);

static const struct c2_bob_type ctx_bob = {
	.bt_name         = "c2_confc_ctx",
	.bt_magix_offset = C2_MAGIX_OFFSET(struct c2_confc_ctx, fc_magic),
	.bt_magix        = C2_CONFC_CTX_MAGIC,
	.bt_check        = _ctx_check
};
C2_BOB_DEFINE(static, &ctx_bob, c2_confc_ctx);

static bool confc_invariant(const struct c2_confc *confc)
{
	C2_PRE(confc != NULL);
	return c2_confc_bob_check(confc);
}

static bool ctx_invariant(const struct c2_confc_ctx *ctx)
{
	C2_PRE(ctx != NULL);
	return c2_confc_ctx_bob_check(ctx);
}

static bool _confc_check(const void *bob)
{
	const struct c2_confc *confc = bob;
	return confc->cc_root != NULL && confc->cc_group != NULL;
}

static bool _ctx_check(const void *bob)
{
	const struct c2_confc_ctx *ctx = bob;
	/* const struct c2_rpc_item  *item = &ctx->fc_fop.f_item; */
	const struct c2_sm        *mach = &ctx->fc_mach;

	return confc_invariant(ctx->fc_confc) &&
		ctx->fc_ast.sa_datum == &ctx->fc_ast_datum &&
#if 0 /* XXX TODO */
		item->ri_type == &request_item_type &&
		item->ri_ops != NULL &&
		item->ri_ops->rio_replied == on_replied &&
		c2_fop_data((struct c2_fop *)&ctx->fc_fop) == &ctx->fc_req &&
#endif
		ctx->fc_clink.cl_cb == on_object_updated &&
		ergo(mach->sm_state == S_TERMINAL, mach->sm_rc == 0) &&
		ergo(mach->sm_state == S_FAILURE, mach->sm_rc < 0);
}

#if 0 /* XXX TODO */
C2_RPC_ITEM_TYPE_DEF(request_item_type, ...XXX...);
#endif

/* ------------------------------------------------------------------
 * c2_confc
 * ------------------------------------------------------------------ */

static int cache_preload(struct c2_confc *confc, const char *conf_str);

/**
 * Skips `prefix' in `str'. Returns NULL if `str' does not start with
 * `prefix'.
 */
static const char *prefix_skip(const char *prefix, const char *str)
{
	while (*prefix != 0 && *str != 0 && *prefix == *str) {
		++prefix;
		++str;
	}
	return *prefix == 0 ? str : NULL;
}

C2_INTERNAL int c2_confc_init(struct c2_confc *confc, const char *conf_source,
			      const struct c2_buf *profile,
			      struct c2_sm_group *sm_group)
{
	const char *s;
	int         rc;

	C2_PRE(conf_source != NULL && *conf_source != 0);
	C2_PRE(c2_buf_is_aimed(profile));
	C2_PRE(sm_group != NULL);

	c2_conf_reg_init(&confc->cc_registry);
	rc = c2_conf_obj_find(&confc->cc_registry, C2_CO_PROFILE, profile,
			      &confc->cc_root); /* here: create a stub */
	if (rc != 0) {
		c2_conf_reg_fini(&confc->cc_registry);
		return rc;
	}
	confc->cc_root->co_confc = confc;
	confc->cc_root->co_mounted = true;

	c2_confc_bob_init(confc);
	confc->cc_group = sm_group;
	c2_mutex_init(&confc->cc_lock);
	confc->cc_nr_ctx = 0;

	s = prefix_skip("local-conf:", conf_source);
	if (s == NULL) {
		/* rc = confd_connect(&confc->cc_rpc, conf_source); */
		C2_IMPOSSIBLE("XXX not implemented");
	} else {
		c2_mutex_lock(&confc->cc_lock);
		rc = cache_preload(confc, s);
		c2_mutex_unlock(&confc->cc_lock);
		C2_ASSERT(equi(rc == 0,
			       confc->cc_root->co_status == C2_CS_READY));
	}

	C2_ASSERT(confc_invariant(confc));
	if (rc != 0)
		c2_confc_fini(confc);
	return rc;
}

C2_INTERNAL void c2_confc_fini(struct c2_confc *confc)
{
	C2_PRE(confc->cc_nr_ctx == 0);

	c2_confc_bob_fini(confc); /* calls _confc_check() */

#if 0 /* XXX TODO */
	if (confc->cc_rpc.rcx_remote_addr != NULL)
		confd_disconnect(&confc->cc_rpc);
#endif

	c2_mutex_fini(&confc->cc_lock);
	confc->cc_group = NULL;
	confc->cc_root = NULL;
	c2_conf_reg_fini(&confc->cc_registry);
}

/* ------------------------------------------------------------------
 * c2_confc_ctx
 * ------------------------------------------------------------------ */

static void conf_group_lock(const struct c2_confc *confc);
static void conf_group_unlock(const struct c2_confc *confc);
static void confc_lock(struct c2_confc *confc);
static void confc_unlock(struct c2_confc *confc);

C2_INTERNAL void c2_confc_ctx_init(struct c2_confc_ctx *ctx,
				   struct c2_confc *confc)
{
	C2_PRE(confc_invariant(confc));

	ctx->fc_confc = confc;
	c2_confc_ctx_bob_init(ctx);

	conf_group_lock(confc); /* needed for c2_sm_init() */
	c2_sm_init(&ctx->fc_mach, &confc_ctx_states_conf, S_INITIAL,
		   confc->cc_group, NULL /* XXX TODO c2_addb_ctx */);

	/* Attach itself to c2_confc. */
	confc_lock(confc);
	C2_CNT_INC(confc->cc_nr_ctx);
	confc_unlock(confc);

	conf_group_unlock(confc);

	ctx->fc_ast.sa_datum = &ctx->fc_ast_datum;
	ctx->fc_origin = NULL;
	ctx->fc_path = NULL;
	c2_clink_init(&ctx->fc_clink, on_object_updated);
	ctx->fc_result = NULL;

	C2_POST(ctx_invariant(ctx));
}

C2_INTERNAL void c2_confc_ctx_fini(struct c2_confc_ctx *ctx)
{
	struct c2_confc *confc = ctx->fc_confc;
	C2_PRE(ctx_invariant(ctx));

	c2_clink_fini(&ctx->fc_clink);

	conf_group_lock(confc); /* needed for c2_sm_fini() */

	confc_lock(confc);
	if (ctx->fc_mach.sm_state == S_TERMINAL && ctx->fc_result != NULL)
		c2_conf_obj_put(ctx->fc_result);
	C2_CNT_DEC(confc->cc_nr_ctx); /* detach from c2_confc */
	confc_unlock(confc);

	c2_sm_fini(&ctx->fc_mach);
	conf_group_unlock(confc);

	c2_confc_ctx_bob_fini(ctx);
	ctx->fc_confc = NULL;
}

C2_INTERNAL bool c2_confc_ctx_is_completed(const struct c2_confc_ctx *ctx)
{
	C2_PRE(ctx_invariant(ctx));
	return C2_IN(ctx->fc_mach.sm_state, (S_TERMINAL, S_FAILURE));
}

C2_INTERNAL int32_t c2_confc_ctx_error(const struct c2_confc_ctx *ctx)
{
	C2_PRE(c2_confc_ctx_is_completed(ctx));
	return ctx->fc_mach.sm_rc;
}

C2_INTERNAL struct c2_conf_obj *c2_confc_ctx_result(struct c2_confc_ctx *ctx)
{
	struct c2_conf_obj *res = ctx->fc_result;

	C2_PRE(ctx_invariant(ctx));
	C2_PRE(ctx->fc_mach.sm_state == S_TERMINAL);
	C2_PRE(res != NULL);

	ctx->fc_result = NULL;
	return res;
}

/* ------------------------------------------------------------------
 * open/close
 * ------------------------------------------------------------------ */

static void ast_state_set(struct c2_sm_ast *ast, enum confc_ctx_state state);

C2_INTERNAL int c2_confc__open(struct c2_confc_ctx *ctx,
			       struct c2_conf_obj *origin,
			       const struct c2_buf path[])
{
	C2_PRE(ctx_invariant(ctx));
	C2_PRE(ergo(origin != NULL, origin->co_confc == ctx->fc_confc));
	C2_PRE(ctx->fc_origin == NULL && ctx->fc_path == NULL);

	ctx->fc_origin = origin == NULL ? ctx->fc_confc->cc_root : origin;
	ctx->fc_path = path;
	ast_state_set(&ctx->fc_ast, S_CHECK);
	return 0;
}

struct sm_waiter {
	struct c2_confc_ctx w_ctx;
	struct c2_clink     w_clink;
};

/** Filters out intermediate state transitions of c2_confc_ctx::fc_mach. */
static bool sm_filter(struct c2_clink *link)
{
	return !c2_confc_ctx_is_completed(&container_of(link, struct sm_waiter,
							w_clink)->w_ctx);
}

C2_INTERNAL int c2_confc__open_sync(struct c2_conf_obj **result,
				    struct c2_conf_obj *origin,
				    const struct c2_buf path[])
{
	struct sm_waiter w;
	int              rc;

	C2_PRE(origin != NULL);

	c2_confc_ctx_init(&w.w_ctx, origin->co_confc);
	c2_clink_init(&w.w_clink, sm_filter);
	c2_clink_add(&w.w_ctx.fc_mach.sm_chan, &w.w_clink);

	rc = c2_confc__open(&w.w_ctx, origin, path);
	if (rc == 0) {
		while (!c2_confc_ctx_is_completed(&w.w_ctx))
			c2_chan_wait(&w.w_clink);

		rc = c2_confc_ctx_error(&w.w_ctx);
		if (rc == 0)
			*result = c2_confc_ctx_result(&w.w_ctx);
	}

	c2_clink_del(&w.w_clink);
	c2_clink_fini(&w.w_clink);
	c2_confc_ctx_fini(&w.w_ctx);

	C2_POST(ergo(rc == 0, (*result)->co_status == C2_CS_READY));
	return rc;
}

C2_INTERNAL void c2_confc_close(struct c2_conf_obj *obj)
{
	if (obj != NULL) {
		confc_lock(obj->co_confc);
		c2_conf_obj_put(obj);
		confc_unlock(obj->co_confc);
	}
}

/* ------------------------------------------------------------------
 * readdir
 * ------------------------------------------------------------------ */

C2_INTERNAL int c2_confc_readdir(struct c2_confc_ctx *ctx,
				 struct c2_conf_obj *dir,
				 struct c2_conf_obj **pptr)
{
	int rc;

	confc_lock(ctx->fc_confc);
	rc = dir->co_ops->coo_readdir(dir, pptr);
	confc_unlock(ctx->fc_confc);

	if (rc == C2_CONF_DIRMISS) {
		C2_IMPOSSIBLE("XXX not implemented");
		/* rc = XXX_initiate_asynchronous_retrieval_of_configuration(); */
	}

	return rc;
}

C2_INTERNAL int c2_confc_readdir_sync(struct c2_conf_obj *dir,
				      struct c2_conf_obj **pptr)
{
	(void)dir;
	(void)pptr;
	C2_IMPOSSIBLE("XXX not implemented");
}

/* ------------------------------------------------------------------
 * State transitions.
 *
 * Note, that *_st_in() functions don't need to assert that the group
 * lock is being hold.  This check is part of state machine invariant
 * (c2_sm_invariant()), which is asserted when a state is entered (and
 * left).
 * ------------------------------------------------------------------ */

static int path_walk(struct c2_confc_ctx *ctx);
static bool request_is_valid(const struct c2_conf_fetch *req);
static struct c2_confc_ctx *mach_to_ctx(struct c2_sm *mach);
static const struct c2_confc_ctx *const_mach_to_ctx(const struct c2_sm *mach);
static bool conf_group_is_locked(const struct c2_confc *confc);
static bool confc_is_locked(const struct c2_confc *confc);

/** Actions to perform on entering S_CHECK state. */
static int check_st_in(struct c2_sm *mach)
{
	static const int next_state[] = {
		[C2_CS_MISSING] = S_WAIT_REPLY,
		[C2_CS_LOADING] = S_WAIT_STATUS,
		[C2_CS_READY]   = S_TERMINAL
	};
	int rc;
	struct c2_confc_ctx *ctx = mach_to_ctx(mach);

	rc = path_walk(ctx);
	if (rc < 0) {
		mach->sm_rc = rc;
		return S_FAILURE;
	}

	C2_ASSERT(IS_IN_ARRAY(rc, next_state));
	return next_state[rc];
}

/** Actions to perform on entering S_WAIT_REPLY state. */
static int wait_reply_st_in(struct c2_sm *mach)
{
	int                  rc;
	struct c2_confc_ctx *ctx = mach_to_ctx(mach);

	C2_PRE(request_is_valid(&ctx->fc_req));

	rc = c2_rpc_post(&ctx->fc_fop.f_item);
	if (rc == 0)
		return -1;

	mach->sm_rc = rc;
	return S_FAILURE;
}

/** Actions to perform on entering S_GROW_CACHE state. */
static int grow_cache_st_in(struct c2_sm *mach)
{
#if 0 /* XXX TODO */
	int                        rc;
	struct c2_conf_fetch_resp *resp;
	struct c2_confc_ctx       *ctx  = mach_to_ctx(mach);
	struct c2_rpc_item        *item = c2_fop_to_rpc_item(&ctx->fc_fop);

	C2_PRE(item->ri_error == 0 && item->ri_reply != NULL);

	resp = c2_fop_data(c2_rpc_item_to_fop(item->ri_reply));
	C2_ASSERT('resp' bob_check()s);

	rc = resp->fr_rc ?: cache_grow(&ctx->fc_confc->cc_registry, resp);

	/* Let rpc layer free the memory allocated for response. */
	c2_rpc_item_put(item->ri_reply); /* XXX */

	if (rc == 0) {
	        return S_CHECK;
	} else {
	        mach->sm_rc = rc;
	        return S_FAILURE;
	}
#else
	(void)mach;
	C2_IMPOSSIBLE("XXX not implemented");
	return -1;
#endif
}

/* /\** Handles `RPC replied' event (i.e. response arrival or an error). *\/ */
/* static void on_replied(struct c2_rpc_item *item) */
/* { */
/* 	struct c2_confc_ctx *ctx = bob_of(c2_rpc_item_to_fop(item), */
/* 					  struct c2_confc_ctx, fc_fop, */
/* 					  &ctx_bob); */
/* 	C2_PRE(item->ri_type == &request_item_type); */

/* 	if (item->ri_error == 0) { */
/* 		c2_rpc_item_get(item->ri_reply); /\* XXX *\/ */
/* 		ast_state_set(&ctx->fc_ast, S_GROW_CACHE); */
/* 	} else { */
/* 		ast_fail(&ctx->fc_ast, item->ri_error); */
/* 	} */
/* } */

/** Handles `object loading completed' and `object unpinned' events. */
static bool on_object_updated(struct c2_clink *link)
{
	struct c2_confc_ctx *ctx = bob_of(link->cl_group, struct c2_confc_ctx,
					  fc_clink, &ctx_bob);
	C2_PRE(confc_is_locked(ctx->fc_confc));

	c2_clink_del(&ctx->fc_clink);
	ast_state_set(&ctx->fc_ast, S_CHECK);
	return true; /* event is consumed */
}

static bool check_st_invariant(const struct c2_sm *mach)
{
	const struct c2_confc_ctx *ctx = const_mach_to_ctx(mach);
	return mach->sm_rc == 0 && ctx->fc_result == NULL &&
		ctx_invariant(ctx);
}

static bool failure_st_invariant(const struct c2_sm *mach)
{
	const struct c2_confc_ctx *ctx = const_mach_to_ctx(mach);
	return ctx->fc_result == NULL && ctx->fc_mach.sm_rc < 0;
}

static bool terminal_st_invariant(const struct c2_sm *mach)
{
	/* We do not check c2_confc_ctx::fc_result, because it may
	 * have been unset by c2_confc_ctx_result(). */
	return mach->sm_rc == 0;
}

/* ------------------------------------------------------------------
 * Walkies.
 *
 *         They're "Techno Trousers". Ex-NASA. Fantastic for walkies!
 * ------------------------------------------------------------------ */

static int path_walk_complete(struct c2_confc_ctx *ctx, struct c2_conf_obj *obj,
			      size_t ri);
static void request_fill(struct c2_confc_ctx *ctx,
			 const struct c2_conf_obj *org, size_t ri);

/** Last path element? */
static bool eop(const struct c2_buf *buf)
{
	C2_PRE(equi(buf->b_nob == 0, buf->b_addr == NULL));
	return buf->b_nob == 0;
}

/**
 * Follows the path, checking statuses of met objects.
 *
 * @retval C2_CS_READY    Path target is reachable.
 *
 * @retval C2_CS_MISSING  One of the intermediate objects or the target
 *                        itself is C2_CS_MISSING.  path_walk()
 *                        changes statuses of such objects to
 *                        C2_CS_LOADING and fills ctx->fc_req.
 *
 * @retval C2_CS_LOADING  Neither path target nor missing objects can
 *                        be reached because of C2_CS_LOADING object
 *                        blocking the path.  path_walk() registers
 *                        ctx->fc_clink with the channel of loading
 *                        object.
 *
 * @retval -ENOENT        ctx->fc_path refers to a nonexistent object.
 *
 * @see @ref confc-lspec-state
 */
static int path_walk(struct c2_confc_ctx *ctx)
{
	int                 ret;
	struct c2_conf_obj *obj;
	size_t              ri;

	C2_PRE(conf_group_is_locked(ctx->fc_confc));
	C2_PRE(ctx->fc_origin != NULL);

	confc_lock(ctx->fc_confc);

	for (ret = 0, obj = ctx->fc_origin, ri = 0;
	     ret == 0 && obj->co_status == C2_CS_READY &&
		     !eop(&ctx->fc_path[ri]);
	     ++ri)
		ret = obj->co_ops->coo_lookup(obj, &ctx->fc_path[ri], &obj);

	if (ret == 0) {
		/*
		 * XXX Confc is operating in offline mode: all the
		 * necessary configuration data has been pre-loaded by
		 * this point.
		 *
		 * TODO: This assertion must be deleted when confd
		 * component is operational.
		 */
		C2_ASSERT(obj->co_status == C2_CS_READY);

		ret = path_walk_complete(ctx, obj, ri);
	}

	confc_unlock(ctx->fc_confc);
	C2_POST(conf_group_is_locked(ctx->fc_confc));
	return ret;
}

/**
 * Applies the results of path walking.
 *
 * @returns original status of the reached configuration object.
 *
 * @param ctx  Configuration retrieval context.
 * @param obj  The object reached by a path walk.
 * @param ri   The position in ctx->fc_path[] where the remaining (not
 *             visited) path components start.
 */
static int
path_walk_complete(struct c2_confc_ctx *ctx, struct c2_conf_obj *obj, size_t ri)
{
	C2_PRE(conf_group_is_locked(ctx->fc_confc));
	C2_PRE(confc_is_locked(ctx->fc_confc));

	switch (obj->co_status) {
	case C2_CS_READY:
		C2_ASSERT(eop(&ctx->fc_path[ri]));
		c2_conf_obj_get(obj);
		ctx->fc_result = obj;
		return C2_CS_READY;

	case C2_CS_MISSING:
		obj->co_status = C2_CS_LOADING;
		if (obj->co_type == C2_CO_DIR) {
			/*
			 * Directory objects don't travel over the
			 * network.  Query the parent object.
			 */
			C2_ASSERT(obj->co_parent != NULL);
			obj = obj->co_parent;
			C2_CNT_DEC(ri);
		}
		request_fill(ctx, obj, ri);
		return C2_CS_MISSING;

	case C2_CS_LOADING:
		c2_clink_add(&obj->co_chan, &ctx->fc_clink);
		return C2_CS_LOADING;

	default:
		C2_IMPOSSIBLE("Invalid object status");
	}
	return -1; /* never reached */
}

/* ------------------------------------------------------------------
 * Casts to c2_confc_ctx
 * ------------------------------------------------------------------ */

static struct c2_confc_ctx *mach_to_ctx(struct c2_sm *mach)
{
	return bob_of(mach, struct c2_confc_ctx, fc_mach, &ctx_bob);
}

static const struct c2_confc_ctx *const_mach_to_ctx(const struct c2_sm *mach)
{
	return bob_of(mach, const struct c2_confc_ctx, fc_mach, &ctx_bob);
}

static struct c2_confc_ctx *ast_to_ctx(struct c2_sm_ast *ast)
{
	return bob_of(ast, struct c2_confc_ctx, fc_ast, &ctx_bob);
}

/* ------------------------------------------------------------------
 * AST
 * ------------------------------------------------------------------ */

static void _state_set(struct c2_sm_group *grp __attribute__((unused)),
		       struct c2_sm_ast *ast)
{
	int state = *(int32_t *)ast->sa_datum;
	C2_PRE(C2_IN(state, (S_INITIAL, S_CHECK, S_WAIT_REPLY, S_WAIT_STATUS,
			     S_GROW_CACHE, /* note the absence of S_FAILURE */
			     S_TERMINAL)));

	c2_sm_state_set(&ast_to_ctx(ast)->fc_mach, state);
}

static void _fail(struct c2_sm_group *grp __attribute__((unused)),
		  struct c2_sm_ast *ast)
{
	c2_sm_fail(&ast_to_ctx(ast)->fc_mach, S_FAILURE,
		   *(int32_t *)ast->sa_datum);
}

static void _ast_post(struct c2_sm_ast *ast,
		      void (*cb)(struct c2_sm_group *, struct c2_sm_ast *),
		      int32_t datum)
{
	struct c2_confc_ctx *ctx = ast_to_ctx(ast);

	ast->sa_cb = cb;
	C2_ASSERT(ast->sa_datum == &ctx->fc_ast_datum);
	ctx->fc_ast_datum = datum;

	c2_sm_ast_post(ctx->fc_confc->cc_group, ast);
}

/** Posts an AST that will advance the state machine to given state. */
static void ast_state_set(struct c2_sm_ast *ast, enum confc_ctx_state state)
{
	_ast_post(ast, _state_set, state);
}

/** Posts an AST that will move the state machine to S_FAILURE state. */
/* XXX static */ C2_INTERNAL void ast_fail(struct c2_sm_ast *ast, int32_t rc)
{
	_ast_post(ast, _fail, rc);
}

/* ------------------------------------------------------------------
 * Configuration cache management
 * ------------------------------------------------------------------ */

static int object_enrich(struct c2_conf_obj *dest,
			 const struct confx_object *src,
			 struct c2_conf_reg *reg);

static struct c2_confc *registry_to_confc(struct c2_conf_reg *reg)
{
	return bob_of(reg, struct c2_confc, cc_registry, &confc_bob);
}

static int
cached_obj_update(struct c2_conf_reg *reg, const struct confx_object *flat)
{
	struct c2_conf_obj *obj;

	C2_PRE(confc_is_locked(registry_to_confc(reg)));

	return c2_conf_obj_find(reg, flat->o_conf.u_type, &flat->o_id, &obj) ?:
		object_enrich(obj, flat, reg);
}

/** Adds objects, described by a configuration string, to the cache. */
static int cache_preload(struct c2_confc *confc, const char *conf_str)
{
	/*
	 * 4096 bytes (kernel module option) / array size (64) = 64
	 * bytes per object.
	 *
	 * XXX TODO: Use dynamic allocation. (To be done by Anatoliy.)
	 */
	static struct confx_object objs[64];
	int rc;
	int nr_objs;
	int i;

	C2_PRE(confc_is_locked(confc));

	rc = nr_objs = c2_conf_parse(conf_str, objs, ARRAY_SIZE(objs));
	if (rc < 0)
		return rc;

	for (i = 0; i < nr_objs; ++i) {
		rc = cached_obj_update(&confc->cc_registry, &objs[i]);
		if (rc != 0)
			break;
	}

	c2_confx_fini(objs, nr_objs);
	return rc;
}

static int object_enrich(struct c2_conf_obj *dest,
			 const struct confx_object *src,
			 struct c2_conf_reg *reg)
{
	int              rc;
	struct c2_confc *confc = registry_to_confc(reg);

	C2_PRE(dest->co_type == src->o_conf.u_type);
	C2_PRE(confc_is_locked(confc));
	C2_PRE(dest->co_confc == NULL || dest->co_confc == confc);

	if (!c2_conf_obj_match(dest, src)) {
		C2_LOG(C2_WARN,
		       "Conflict of incoming and cached configuration data");
		return -EPROTO;
	}

	if (dest->co_status == C2_CS_READY)
		return 0; /* do nothing */

	if (dest->co_confc == NULL)
		dest->co_confc = confc;

	rc = c2_conf_obj_fill(dest, src, reg);
	if (rc != 0)
		dest->co_status = C2_CS_MISSING;
	c2_chan_broadcast(&dest->co_chan);

	return rc;
}

/* /\** */
/*  * Adds new objects, contained in confd's response, to the */
/*  * configuration cache. */
/*  * */
/*  * @pre  resp->fr_rc == 0 */
/*  *\/ */
/* static int */
/* cache_grow(struct c2_conf_reg *reg, const struct c2_conf_fetch_resp *resp) */
/* { */
/* 	int                  ret; */
/* 	struct confx_object *flat; */
/* 	struct c2_conf_obj  *cached; */
/* 	struct c2_confc     *confc = registry_to_confc(reg); */

/* 	C2_PRE(resp->fr_rc == 0); */
/* 	C2_PRE(conf_group_is_locked(confc)); */

/* 	confc_lock(confc); */
/* 	for (flat in resp->fr_data) { */
/* 		if (flat->o_id.b_nob == 0 || flat->o_id.b_addr == NULL) { */
/* 			C2_ADDB_ADD(report bogus data); */
/* 			ret = -Exxx; */
/* 			break; */
/* 		} */

/* 		ret = cached_obj_update(reg, flat); */
/* 		if (ret != 0) */
/* 			break; */
/* 	} */
/* 	confc_unlock(confc); */
/* 	return ret; */
/* } */

/* ------------------------------------------------------------------
 * misc
 * ------------------------------------------------------------------ */

/* static int confd_connect(struct c2_rpc_client_ctx *rpc, const char *confd_addr) */
/* { */
/* 	int rc; */

/* 	rpc->rcx_remote_addr = confd_addr; */
/* 	XXX; /\* set other fields of `rpc' *\/ */

/* 	rc = c2_rpc_client_start(rpc); */
/* 	if (rc != 0) { */
/* 		(void) c2_rpc_client_stop(rpc); */
/* 		rpc->rcx_remote_addr = NULL; */
/* 	} */
/* 	return rc; */
/* } */

/* static void confd_disconnect(struct c2_rpc_client_ctx *rpc) */
/* { */
/* 	int rc; */

/* 	C2_PRE(rpc->rcx_remote_addr != NULL); */

/* 	rc = c2_rpc_client_stop(rpc); */
/* 	if (rc != 0) */
/* 		C2_ADDB(report error); */
/* } */

static bool request_is_valid(const struct c2_conf_fetch *req)
{
	return  req->f_origin.oi_type < C2_CO_NR &&
		c2_buf_is_aimed(&req->f_origin.oi_id) &&
		equi(req->f_path.ab_count == 0, req->f_path.ab_elems == NULL);
}

/**
 * Fills c2_conf_fetch structure.
 *
 * @param ctx  Configuration retrieval context.
 * @param org  Origin of the path being sent to confd.
 * @param ri   The position (in ctx->fc_path[]) that starts the path
 *             being sent to confd.
 */
static void
request_fill(struct c2_confc_ctx *ctx, const struct c2_conf_obj *org, size_t ri)
{
	uint32_t              len;
	struct c2_conf_fetch *req = &ctx->fc_req;
	const struct c2_buf  *path = &ctx->fc_path[ri];

	C2_PRE(ctx_invariant(ctx));

	req->f_origin.oi_type = org->co_type;
	req->f_origin.oi_id = org->co_id;

	for (len = 0; !eop(&ctx->fc_path[ri + len]); ++len)
		; /* measure path length */
	req->f_path.ab_count = len;
	req->f_path.ab_elems = len == 0 ? NULL :
		(struct c2_buf *)path; /* strip const */

	C2_POST(request_is_valid(req));
}

/* ------------------------------------------------------------------
 * Locking
 * ------------------------------------------------------------------ */

static void conf_group_lock(const struct c2_confc *confc)
{
	c2_mutex_lock(&confc->cc_group->s_lock);
}

static void conf_group_unlock(const struct c2_confc *confc)
{
	c2_mutex_unlock(&confc->cc_group->s_lock);
}

static bool conf_group_is_locked(const struct c2_confc *confc)
{
	return c2_mutex_is_locked(&confc->cc_group->s_lock);
}

static void confc_lock(struct c2_confc *confc)
{
	c2_mutex_lock(&confc->cc_lock);
}

static void confc_unlock(struct c2_confc *confc)
{
	c2_mutex_unlock(&confc->cc_lock);
}

static bool confc_is_locked(const struct c2_confc *confc)
{
	return c2_mutex_is_locked(&confc->cc_lock);
}

/* ------------------------------------------------------------------
 * Confc kernel test
 * ------------------------------------------------------------------ */

#ifdef __KERNEL__
int c2t1fs_conf_test(const char *buf)
{
	C2_INTERNAL void c2_conf__reg2dot(const struct c2_conf_reg *reg);
	int                  i;
	int                  n;
	int                  rc;
	struct confx_object *conf;
	struct c2_conf_obj  *obj;
	struct c2_conf_reg   reg;

	n = c2_confx_obj_nr(buf);
	if (n <= 0) {
		rc = n;
		goto conf_cleanup;
	}

	C2_ALLOC_ARR(conf, n);
	if (conf == NULL) {
		rc = -ENOMEM;
		goto conf_cleanup;
	}

	rc = c2_conf_parse(buf, conf, n);
	if (rc <= 0)
		goto conf_free;


	c2_conf_reg_init(&reg);
	for (i = 0; i < n; ++i) {
		rc = c2_conf_obj_find(&reg, conf[i].o_conf.u_type,
				      &conf[i].o_id, &obj);
		if (rc != 0)
			break;

		rc = c2_conf_obj_fill(obj, &conf[i], &reg);
		if (rc != 0)
			break;
	}

	c2_conf__reg2dot(&reg);
	c2_conf_reg_fini(&reg);
	c2_confx_fini(conf, n);

conf_free:
	c2_free(conf);
conf_cleanup:
	return rc;
}
#endif /* __KERNEL__ */

#undef C2_TRACE_SUBSYSTEM

/** @} confc_dlspec */
