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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/confc.h"
#include "conf/obj_ops.h"
#include "conf/preload.h"   /* m0_conf_parse */
#include "conf/buf_ext.h"   /* m0_buf_is_aimed */
#include "conf/conf_fop.h"  /* m0_conf_fetch_fopt */
#include "mero/magic.h"  /* M0_CONFC_MAGIC, M0_CONFC_CTX_MAGIC */
#include "rpc/rpc.h"        /* m0_rpc_post */
#include "rpc/rpclib.h"     /* m0_rpc_client_call */
#include "lib/cdefs.h"      /* M0_HAS_TYPE */
#include "lib/arith.h"      /* M0_CNT_INC, M0_CNT_DEC */
#include "lib/misc.h"       /* M0_IN */
#include "lib/errno.h"      /* ENOMEM, EPROTO */
#include "lib/memory.h"     /* M0_ALLOC_ARR, m0_free */

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
 * A state machine is embedded into m0_confc_ctx structure as its @ref
 * m0_confc_ctx::fc_mach "fc_mach" member.  m0_confc_ctx_init()
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
 *   "path target is reached\nand it is M0_CS_READY"];
 *     S_CHECK -> S_FAILURE [label="error"];
 *     S_CHECK -> S_WAIT_REPLY [label=
 *   "M0_CS_MISSING object\nis reached\n{ set status to M0_CS_LOADING;\nsend request }"];
 *     S_WAIT_REPLY -> S_FAILURE [label="timeout\nor\nrc != 0"];
 *     S_WAIT_REPLY -> S_GROW_CACHE [label="response received,\nrc == 0"];
 *     S_GROW_CACHE -> S_FAILURE [label="error"];
 *     S_GROW_CACHE -> S_CHECK [label="done"];
 *     S_CHECK -> S_WAIT_STATUS [label=
 *   "M0_CS_LOADING object\nis reached\n{ m0_clink_add(&obj->co_chan) }"];
 *     S_WAIT_STATUS -> S_CHECK [label="\"status updated\" event"];
 * }
 * @enddot
 *
 * @subsection confc-lspec-state-initial S_INITIAL
 *
 * Summary: m0_confc_ctx has just been initialised.
 *
 * m0_confc_open() populates m0_confc_ctx::fc_path array (path_store())
 * and posts an AST to m0_confc::cc_group.
 *
 * @note  m0_sm_ast_post() signals group's clink. Current design of
 *        confc assumes that some thread will respond to this event by
 *        calling m0_sm_asts_run().
 *
 * When the AST, posted by m0_confc_open(), is run, it moves a state
 * machine (m0_confc_ctx::fc_mach) to S_CHECK state.
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
|    M0_CS_READY     |  S_TERMINAL     |
|    M0_CS_MISSING   |  S_WAIT_REPLY   |
|    M0_CS_LOADING   |  S_WAIT_STATUS  |
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
 * configuration request (m0_confc_ctx::fc_req) to the confd, using
 * m0_rpc_post().
 *
 * A state machine remains in S_WAIT_REPLY state until a reply from
 * confd arrives. This event triggers on_replied() callback.  If
 * m0_rpc_item::ri_error is non-zero, on_replied() posts an AST that
 * will eventually move the state machine to S_FAILURE state.  If
 * ->ri_error is zero, on_replied() increments rpc item's reference
 * counter (m0_rpc_item_get()) and posts an AST, scheduling transition
 * to S_GROW_CACHE state.
 *
 * @subsection confc-lspec-state-wait-status S_WAIT_STATUS
 *
 * Summary: Waiting for an object to be filled by another
 * configuration request.
 *
 * A state machine in S_WAIT_STATUS state remains idle until the
 * channel (m0_conf_obj::co_chan) that m0_confc_ctx::fc_clink is
 * registered with is signaled.  Such an event triggers
 * on_object_updated() callback, which de-registers the clink and
 * posts an AST that will eventually move the state machine to S_CHECK
 * state.
 *
 * @note  Object's channel (m0_conf_obj::co_chan) is signaled
 *        (m0_chan_broadcast()) when
 *        -#
 *          object_enrich() completes loading of configuration data
 *          into this object and changes its status to M0_CS_READY
 *          (loading succeeded) or M0_CS_MISSING (loading failed);
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
 * contained in confd's response (m0_conf_fetch_resp::fr_rc) is zero,
 * the callback calls cache_grow() function (see @ref
 * confc-lspec-grow below).  The callback "releases" rpc item by
 * calling m0_rpc_item_put().  If ->fr_rc == 0 and cache_grow()
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
 * path_walk() begins with locking the confc cache (m0_confc::cc_lock);
 * it unlocks the cache before returning.
 *
 * The function "moves" along the DAG of cached configuration objects,
 * starting at m0_confc_ctx::fc_origin object and following
 * m0_confc_ctx::fc_path.  Next object is found by calling
 * m0_conf_obj_ops::coo_lookup() with current object and path
 * component as parameters.  The iteration continues until
 * ->coo_lookup() fails, or a stub is met, or the end of path is
 * reached.
 *
 * path_walk_complete() applies the results of path walking:
 * increments reference counter of M0_CS_READY object, fills
 * m0_conf_fetch request for M0_CS_MISSING object, or registers clink
 * with the channel of M0_CS_LOADING object.
 *
 * <hr> <!------------------------------------------------------------>
 * @section confc-lspec-grow Growing the cache
 *
 * cache_grow() locks the cache (m0_confc::cc_lock) and unlocks before
 * returning.  The function performs the following operations for
 * every object descriptor (confx_object, defined in conf/onwire.ff):
 *   -#
 *      Tries to find an object with the same identity (type and id)
 *      in the registry of cached objects (m0_confc::cc_registry),
 *      using m0_conf_reg_lookup().
 *   -#
 *      If cached object is found, it is "enriched" (see
 *      object_enrich() below).  Otherwise new object is added to the
 *      cache with cache_add().
 *
 * cache_add() performs the following operations:
 *   -# m0_conf_obj_create() --- allocates configuration object and
 *      initialises its fields;
 *   -# m0_conf_obj_fill() --- fills new object with configuration data
 *      contained in on-wire object descriptor (confx_object);
 *   -# m0_conf_reg_add() --- adds configuration object to the
 *      registry of cached objects.
 *
 * object_enrich() compares cached object with the descriptor received
 * from the confd.  If a discrepancy is found (!m0_conf_obj_match()),
 * the function reports it (M0_ADDB_ADD()) and returns an error code.
 *
 * If there is no discrepancy, and the cached object is a stub,
 * object_enrich()
 *   - fills the cached object with configuration data
 *     (m0_conf_obj_fill());
 *   - changes status of the cached object to M0_CS_READY or
 *     M0_CS_MISSING, depending on whether filling was successful;
 *   - signals object's channel.
 *
 * <hr> <!------------------------------------------------------------>
 * @section confc-lspec-thread Threading and Concurrency Model
 *
 * There are as many state machines in operation as there are
 * unfinished m0_confc_open*() requests.
 *
 * At most one state transition (m0_sm_state_descr::sd_in()) can be
 * running at any given time.  Synchronization of state transitions is
 * achieved by using m0_sm_group (m0_confc::cc_group).
 *
 * Modifications to m0_confc instance and confc cache are protected by
 * m0_confc::cc_lock mutex, aka confc lock.
 *
 * If a function needs both locks -- group lock and confc lock -- for
 * its operation, group lock must be acquired first. (Note, that the
 * "function" here cannot be something invoked from an AST callback,
 * because otherwise it would deadlock on the group mutex.)
 *
 * A user managing the state machine group (m0_confc::cc_group) is
 * responsible for making sure m0_sm_asts_run() is called when
 * m0_sm_group::s_clink is signaled.  See @ref sm (search for `"ast"
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

static int check_st_in(struct m0_sm *mach);      /* S_CHECK */
static int wait_reply_st_in(struct m0_sm *mach); /* S_WAIT_REPLY */
static int grow_cache_st_in(struct m0_sm *mach); /* S_GROW_CACHE */

static bool check_st_invariant(const struct m0_sm *mach);    /* S_CHECK */
static bool failure_st_invariant(const struct m0_sm *mach);  /* S_FAILURE */
static bool terminal_st_invariant(const struct m0_sm *mach); /* S_TERMINAL */

/** States of m0_confc_ctx::fc_mach. */
enum confc_ctx_state { S_INITIAL, S_CHECK, S_WAIT_REPLY, S_WAIT_STATUS,
		       S_GROW_CACHE, S_FAILURE, S_TERMINAL, S_NR };

static const struct m0_sm_state_descr confc_ctx_states[S_NR] = {
	[S_INITIAL] = {
		.sd_flags     = M0_SDF_INITIAL,
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
		.sd_flags     = M0_SDF_FAILURE,
		.sd_name      = "S_FAILURE",
		.sd_in        = NULL,
		.sd_ex        = NULL,
		.sd_invariant = failure_st_invariant,
		.sd_allowed   = 0
	},
	[S_TERMINAL] = {
		.sd_flags     = M0_SDF_TERMINAL,
		.sd_name      = "S_TERMINAL",
		.sd_in        = NULL,
		.sd_ex        = NULL,
		.sd_invariant = terminal_st_invariant,
		.sd_allowed   = 0
	}
};

static const struct m0_sm_conf confc_ctx_states_conf = {
	.scf_name      = "states of m0_confc_ctx::fc_mach",
	.scf_nr_states = S_NR,
	.scf_state     = confc_ctx_states
};

/* ------------------------------------------------------------------
 * Bob types and invariants
 * ------------------------------------------------------------------ */

static bool _confc_check(const void *bob);
static bool _ctx_check(const void *bob);
static bool on_object_updated(struct m0_clink *link);
static void on_replied(struct m0_rpc_item *item);

static const struct m0_bob_type confc_bob = {
	.bt_name         = "m0_confc",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_confc, cc_magic),
	.bt_magix        = M0_CONFC_MAGIC,
	.bt_check        = _confc_check
};
M0_BOB_DEFINE(static, &confc_bob, m0_confc);

static const struct m0_bob_type ctx_bob = {
	.bt_name         = "m0_confc_ctx",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_confc_ctx, fc_magic),
	.bt_magix        = M0_CONFC_CTX_MAGIC,
	.bt_check        = _ctx_check
};
M0_BOB_DEFINE(static, &ctx_bob, m0_confc_ctx);

static bool confc_invariant(const struct m0_confc *confc)
{
	return m0_confc_bob_check(confc);
}

static bool ctx_invariant(const struct m0_confc_ctx *ctx)
{
	M0_PRE(ctx != NULL);
	return m0_confc_ctx_bob_check(ctx);
}

/* ------------------------------------------------------------------
 * m0_confc
 * ------------------------------------------------------------------ */

static int cache_preload(struct m0_confc *confc, const char *local_conf);
static bool online(const struct m0_confc *confc);
static int connect_to_confd(struct m0_confc *confc, const char *confd_addr,
			    struct m0_rpc_machine *rpc_mach);
static void disconnect_from_confd(struct m0_confc *confc);
static int root_create(struct m0_confc *confc, const struct m0_buf *profile);

static bool not_empty(const char *s)
{
	return s != NULL && *s != '\0';
}

M0_INTERNAL int m0_confc_init(struct m0_confc       *confc,
			      struct m0_sm_group    *sm_group,
			      const struct m0_buf   *profile,
			      const char            *confd_addr,
			      struct m0_rpc_machine *rpc_mach,
			      const char            *local_conf)
{
	int rc;

	M0_ENTRY();
	M0_PRE(not_empty(confd_addr) || not_empty(local_conf));
	M0_PRE(sm_group != NULL);
	M0_PRE(ergo(not_empty(confd_addr), rpc_mach != NULL));

	rc = root_create(confc, profile);
	if (rc != 0)
		M0_RETURN(rc);

	m0_confc_bob_init(confc);
	confc->cc_group = sm_group;
	confc->cc_nr_ctx = 0;
	m0_mutex_init(&confc->cc_lock);

	if (not_empty(local_conf))
		rc = cache_preload(confc, local_conf);

	M0_SET0(&confc->cc_rpc_conn);
	M0_ASSERT(!online(confc));
	if (rc == 0 && not_empty(confd_addr))
		rc = connect_to_confd(confc, confd_addr, rpc_mach);

	if (rc == 0) {
		M0_POST(confc_invariant(confc));
		M0_RETURN(rc);
	}

	m0_mutex_fini(&confc->cc_lock);
	confc->cc_root = NULL;
	m0_conf_reg_fini(&confc->cc_registry);

	M0_RETURN(rc);
}

M0_INTERNAL void m0_confc_fini(struct m0_confc *confc)
{
	M0_ENTRY();
	M0_PRE(confc->cc_nr_ctx == 0);

	m0_confc_bob_fini(confc); /* calls _confc_check() */

	if (online(confc))
		disconnect_from_confd(confc);

	m0_mutex_fini(&confc->cc_lock);
	confc->cc_group = NULL;
	confc->cc_root = NULL;
	m0_conf_reg_fini(&confc->cc_registry);
	M0_LEAVE();
}

static bool _confc_check(const void *bob)
{
	const struct m0_confc *confc = bob;
	return confc->cc_root != NULL && confc->cc_group != NULL;
}

/** Initialises confc->cc_registry and creates a stub of confc->cc_root. */
static int root_create(struct m0_confc *confc, const struct m0_buf *profile)
{
	int rc;

	M0_ENTRY();
	M0_PRE(m0_buf_is_aimed(profile));

	m0_conf_reg_init(&confc->cc_registry);
	rc = m0_conf_obj_find(&confc->cc_registry, M0_CO_PROFILE, profile,
			      &confc->cc_root); /* creates a stub */
	if (rc == 0) {
		confc->cc_root->co_confc = confc;
		confc->cc_root->co_mounted = true;
	} else {
		m0_conf_reg_fini(&confc->cc_registry);
	}
	M0_RETURN(rc);
}

/* ------------------------------------------------------------------
 * m0_confc_ctx
 * ------------------------------------------------------------------ */

static void conf_group_lock(const struct m0_confc *confc);
static void conf_group_unlock(const struct m0_confc *confc);
static void confc_lock(struct m0_confc *confc);
static void confc_unlock(struct m0_confc *confc);
static void req_fop_init(struct m0_confc_ctx *ctx);
static void req_fop_fini(struct m0_confc_ctx *ctx);
static bool req_fop_invariant(const struct m0_confc_ctx *ctx);

M0_INTERNAL void m0_confc_ctx_init(struct m0_confc_ctx *ctx,
				   struct m0_confc *confc)
{
	M0_PRE(confc_invariant(confc));

	ctx->fc_confc = confc;
	m0_confc_ctx_bob_init(ctx);

	conf_group_lock(confc); /* needed for m0_sm_init() */
	m0_sm_init(&ctx->fc_mach, &confc_ctx_states_conf, S_INITIAL,
		   confc->cc_group, NULL /* XXX TODO m0_addb_ctx */);

	/* Attach itself to m0_confc. */
	confc_lock(confc);
	M0_CNT_INC(confc->cc_nr_ctx);
	confc_unlock(confc);

	conf_group_unlock(confc);

	ctx->fc_ast.sa_datum = &ctx->fc_ast_datum;
	ctx->fc_origin = NULL;
	ctx->fc_path = NULL;

	req_fop_init(ctx);
	m0_clink_init(&ctx->fc_clink, on_object_updated);
	ctx->fc_result = NULL;

	M0_POST(ctx_invariant(ctx));
}

M0_INTERNAL void m0_confc_ctx_fini(struct m0_confc_ctx *ctx)
{
	struct m0_confc *confc = ctx->fc_confc;
	M0_PRE(ctx_invariant(ctx));

	m0_clink_fini(&ctx->fc_clink);
	req_fop_fini(ctx);

	conf_group_lock(confc); /* needed for m0_sm_fini() */

	confc_lock(confc);
	if (ctx->fc_mach.sm_state == S_TERMINAL && ctx->fc_result != NULL)
		m0_conf_obj_put(ctx->fc_result);
	M0_CNT_DEC(confc->cc_nr_ctx); /* detach from m0_confc */
	confc_unlock(confc);

	m0_sm_fini(&ctx->fc_mach);
	conf_group_unlock(confc);

	m0_confc_ctx_bob_fini(ctx);
	ctx->fc_confc = NULL;
}

static bool _ctx_check(const void *bob)
{
	const struct m0_confc_ctx *ctx = bob;
	const struct m0_sm        *mach = &ctx->fc_mach;

	return  confc_invariant(ctx->fc_confc) &&
		ctx->fc_ast.sa_datum == &ctx->fc_ast_datum &&
		ctx->fc_clink.cl_cb == on_object_updated &&
		req_fop_invariant(ctx) &&
		ergo(mach->sm_state == S_TERMINAL, mach->sm_rc == 0) &&
		ergo(mach->sm_state == S_FAILURE, mach->sm_rc < 0);
}

M0_INTERNAL bool m0_confc_ctx_is_completed(const struct m0_confc_ctx *ctx)
{
	M0_PRE(ctx_invariant(ctx));
	return M0_IN(ctx->fc_mach.sm_state, (S_TERMINAL, S_FAILURE));
}

M0_INTERNAL int32_t m0_confc_ctx_error(const struct m0_confc_ctx *ctx)
{
	M0_PRE(m0_confc_ctx_is_completed(ctx));
	return ctx->fc_mach.sm_rc;
}

M0_INTERNAL struct m0_conf_obj *m0_confc_ctx_result(struct m0_confc_ctx *ctx)
{
	struct m0_conf_obj *res = ctx->fc_result;

	M0_PRE(ctx_invariant(ctx));
	M0_PRE(ctx->fc_mach.sm_state == S_TERMINAL);
	M0_PRE(res != NULL);

	ctx->fc_result = NULL;
	return res;
}

/* ------------------------------------------------------------------
 * open/close
 * ------------------------------------------------------------------ */

static void ast_state_set(struct m0_sm_ast *ast, enum confc_ctx_state state);

M0_INTERNAL int m0_confc__open(struct m0_confc_ctx *ctx,
			       struct m0_conf_obj *origin,
			       const struct m0_buf path[])
{
	M0_PRE(ctx_invariant(ctx));
	M0_PRE(ergo(origin != NULL, origin->co_confc == ctx->fc_confc));
	M0_PRE(ctx->fc_origin == NULL && ctx->fc_path == NULL);

	ctx->fc_origin = origin == NULL ? ctx->fc_confc->cc_root : origin;
	ctx->fc_path = path;
	ast_state_set(&ctx->fc_ast, S_CHECK);
	return 0;
}

struct sm_waiter {
	struct m0_confc_ctx w_ctx;
	struct m0_clink     w_clink;
};

/** Filters out intermediate state transitions of m0_confc_ctx::fc_mach. */
static bool sm_filter(struct m0_clink *link)
{
	return !m0_confc_ctx_is_completed(&container_of(link, struct sm_waiter,
							w_clink)->w_ctx);
}

M0_INTERNAL int m0_confc__open_sync(struct m0_conf_obj **result,
				    struct m0_conf_obj *origin,
				    const struct m0_buf path[])
{
	struct sm_waiter w;
	int              rc;

	M0_PRE(origin != NULL);

	m0_confc_ctx_init(&w.w_ctx, origin->co_confc);
	m0_clink_init(&w.w_clink, sm_filter);
	m0_clink_add(&w.w_ctx.fc_mach.sm_chan, &w.w_clink);

	rc = m0_confc__open(&w.w_ctx, origin, path);
	if (rc == 0) {
		while (!m0_confc_ctx_is_completed(&w.w_ctx))
			m0_chan_wait(&w.w_clink);

		rc = m0_confc_ctx_error(&w.w_ctx);
		if (rc == 0)
			*result = m0_confc_ctx_result(&w.w_ctx);
	}

	m0_clink_del(&w.w_clink);
	m0_clink_fini(&w.w_clink);
	m0_confc_ctx_fini(&w.w_ctx);

	M0_POST(ergo(rc == 0, (*result)->co_status == M0_CS_READY));
	return rc;
}

M0_INTERNAL void m0_confc_close(struct m0_conf_obj *obj)
{
	if (obj != NULL) {
		confc_lock(obj->co_confc);
		m0_conf_obj_put(obj);
		confc_unlock(obj->co_confc);
	}
}

/* ------------------------------------------------------------------
 * readdir
 * ------------------------------------------------------------------ */

M0_INTERNAL int m0_confc_readdir(struct m0_confc_ctx *ctx,
				 struct m0_conf_obj *dir,
				 struct m0_conf_obj **pptr)
{
	int rc;

	confc_lock(ctx->fc_confc);
	rc = dir->co_ops->coo_readdir(dir, pptr);
	confc_unlock(ctx->fc_confc);

	if (rc == M0_CONF_DIRMISS) {
		M0_IMPOSSIBLE("XXX not implemented");
		/* rc = XXX_initiate_asynchronous_retrieval_of_configuration(); */
	}

	return rc;
}

M0_INTERNAL int m0_confc_readdir_sync(struct m0_conf_obj *dir,
				      struct m0_conf_obj **pptr)
{
	(void)dir;
	(void)pptr;
	M0_IMPOSSIBLE("XXX not implemented");
}

/* ------------------------------------------------------------------
 * State transitions
 *
 * Note, that *_st_in() functions don't need to assert that the group
 * lock is being hold.  This check is part of state machine invariant
 * (m0_sm_invariant()), which is asserted when a state is entered (and
 * left).
 * ------------------------------------------------------------------ */

static int path_walk(struct m0_confc_ctx *ctx);
static bool request_is_valid(const struct m0_conf_fetch *req);
static struct m0_confc_ctx *mach_to_ctx(struct m0_sm *mach);
static const struct m0_confc_ctx *const_mach_to_ctx(const struct m0_sm *mach);
static bool conf_group_is_locked(const struct m0_confc *confc);
static bool confc_is_locked(const struct m0_confc *confc);
static int cache_grow(struct m0_conf_reg *reg,
		      const struct m0_conf_fetch_resp *resp);
static void ast_fail(struct m0_sm_ast *ast, int32_t rc);

/** Actions to perform on entering S_CHECK state. */
static int check_st_in(struct m0_sm *mach)
{
	static const int next_state[] = {
		[M0_CS_MISSING] = S_WAIT_REPLY,
		[M0_CS_LOADING] = S_WAIT_STATUS,
		[M0_CS_READY]   = S_TERMINAL
	};
	int rc;
	struct m0_confc_ctx *ctx = mach_to_ctx(mach);

	rc = path_walk(ctx);
	if (rc < 0) {
		mach->sm_rc = rc;
		return S_FAILURE;
	}

	M0_ASSERT(IS_IN_ARRAY(rc, next_state));
	return next_state[rc];
}

/** Actions to perform on entering S_WAIT_REPLY state. */
static int wait_reply_st_in(struct m0_sm *mach)
{
	enum { TIME_TO_WAIT = 5 };
	int                  rc;
	struct m0_confc_ctx *ctx = mach_to_ctx(mach);
	struct m0_rpc_item  *item = &ctx->fc_fop.f_item;

	M0_PRE(req_fop_invariant(ctx) && request_is_valid(&ctx->fc_req));

	item->ri_op_timeout = m0_time_from_now(TIME_TO_WAIT, 0);
	rc = m0_rpc_post(item);
	if (rc == 0)
		return -1;

	mach->sm_rc = rc;
	return S_FAILURE;
}

/** Actions to perform on entering S_GROW_CACHE state. */
static int grow_cache_st_in(struct m0_sm *mach)
{
	int                        rc;
	struct m0_conf_fetch_resp *resp;
	struct m0_confc_ctx       *ctx  = mach_to_ctx(mach);
	struct m0_rpc_item        *item = &ctx->fc_fop.f_item;

	M0_PRE(item->ri_error == 0 && item->ri_reply != NULL);

	resp = m0_fop_data(m0_rpc_item_to_fop(item->ri_reply));
	rc = resp->fr_rc ?: cache_grow(&ctx->fc_confc->cc_registry, resp);

	/* Let the rpc layer free memory allocated for response. */
	m0_rpc_item_put(item->ri_reply); /* XXX */

	if (rc == 0)
	        return S_CHECK;

	mach->sm_rc = rc;
	return S_FAILURE;

}

/** Handles `RPC replied' event (i.e. response arrival or an error). */
static void on_replied(struct m0_rpc_item *item)
{
	struct m0_confc_ctx *ctx = bob_of(m0_rpc_item_to_fop(item),
					  struct m0_confc_ctx, fc_fop,
					  &ctx_bob);
	M0_PRE(ctx_invariant(ctx));

	if (item->ri_error == 0) {
		m0_rpc_item_get(item->ri_reply); /* XXX */
		ast_state_set(&ctx->fc_ast, S_GROW_CACHE);
	} else {
		ast_fail(&ctx->fc_ast, item->ri_error);
	}
}

/** Handles `object loading completed' and `object unpinned' events. */
static bool on_object_updated(struct m0_clink *link)
{
	struct m0_confc_ctx *ctx = bob_of(link->cl_group, struct m0_confc_ctx,
					  fc_clink, &ctx_bob);
	M0_PRE(confc_is_locked(ctx->fc_confc));

	m0_clink_del(&ctx->fc_clink);
	ast_state_set(&ctx->fc_ast, S_CHECK);
	return true; /* event is consumed */
}

static bool check_st_invariant(const struct m0_sm *mach)
{
	const struct m0_confc_ctx *ctx = const_mach_to_ctx(mach);
	return mach->sm_rc == 0 && ctx->fc_result == NULL &&
		ctx_invariant(ctx);
}

static bool failure_st_invariant(const struct m0_sm *mach)
{
	const struct m0_confc_ctx *ctx = const_mach_to_ctx(mach);
	return ctx->fc_result == NULL && ctx->fc_mach.sm_rc < 0;
}

static bool terminal_st_invariant(const struct m0_sm *mach)
{
	/* We do not check m0_confc_ctx::fc_result, because it may
	 * have been unset by m0_confc_ctx_result(). */
	return mach->sm_rc == 0;
}

/* ------------------------------------------------------------------
 * Walkies
 *
 *         They're "Techno Trousers". Ex-NASA. Fantastic for walkies!
 * ------------------------------------------------------------------ */

static int path_walk_complete(struct m0_confc_ctx *ctx, struct m0_conf_obj *obj,
			      size_t ri);
static void request_fill(struct m0_confc_ctx *ctx,
			 const struct m0_conf_obj *org, size_t ri);

/** Last path element? */
static bool eop(const struct m0_buf *buf)
{
	M0_PRE(equi(buf->b_nob == 0, buf->b_addr == NULL));
	return buf->b_nob == 0;
}

/**
 * Follows the path, checking statuses of met objects.
 *
 * @retval M0_CS_READY    Path target is reachable.
 *
 * @retval M0_CS_MISSING  One of the intermediate objects or the target
 *                        itself is M0_CS_MISSING.  path_walk()
 *                        changes statuses of such objects to
 *                        M0_CS_LOADING and fills ctx->fc_req.
 *
 * @retval M0_CS_LOADING  Neither path target nor missing objects can
 *                        be reached because of M0_CS_LOADING object
 *                        blocking the path.  path_walk() registers
 *                        ctx->fc_clink with the channel of loading
 *                        object.
 *
 * @retval -ENOENT        ctx->fc_path refers to a nonexistent object.
 *
 * @see @ref confc-lspec-state
 */
static int path_walk(struct m0_confc_ctx *ctx)
{
	int                 ret;
	struct m0_conf_obj *obj;
	size_t              ri;

	M0_PRE(conf_group_is_locked(ctx->fc_confc));
	M0_PRE(ctx->fc_origin != NULL);

	confc_lock(ctx->fc_confc);

	for (ret = 0, obj = ctx->fc_origin, ri = 0;
	     ret == 0 && obj->co_status == M0_CS_READY &&
		     !eop(&ctx->fc_path[ri]);
	     ++ri)
		ret = obj->co_ops->coo_lookup(obj, &ctx->fc_path[ri], &obj);

	if (ret == 0)
		ret = path_walk_complete(ctx, obj, ri);

	confc_unlock(ctx->fc_confc);
	M0_POST(conf_group_is_locked(ctx->fc_confc));
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
path_walk_complete(struct m0_confc_ctx *ctx, struct m0_conf_obj *obj, size_t ri)
{
	M0_PRE(conf_group_is_locked(ctx->fc_confc));
	M0_PRE(confc_is_locked(ctx->fc_confc));

	switch (obj->co_status) {
	case M0_CS_READY:
		M0_ASSERT(eop(&ctx->fc_path[ri]));
		m0_conf_obj_get(obj);
		ctx->fc_result = obj;
		return M0_CS_READY;

	case M0_CS_MISSING:
		obj->co_status = M0_CS_LOADING;
		if (obj->co_type == M0_CO_DIR) {
			/*
			 * Directory objects don't travel over the
			 * network.  Query the parent object.
			 */
			M0_ASSERT(obj->co_parent != NULL);
			obj = obj->co_parent;
			M0_CNT_DEC(ri);
		}
		request_fill(ctx, obj, ri);
		return M0_CS_MISSING;

	case M0_CS_LOADING:
		m0_clink_add(&obj->co_chan, &ctx->fc_clink);
		return M0_CS_LOADING;

	default:
		M0_IMPOSSIBLE("Invalid object status");
	}
	return -1; /* never reached */
}

/* ------------------------------------------------------------------
 * Casts to m0_confc_ctx
 * ------------------------------------------------------------------ */

static struct m0_confc_ctx *mach_to_ctx(struct m0_sm *mach)
{
	return bob_of(mach, struct m0_confc_ctx, fc_mach, &ctx_bob);
}

static const struct m0_confc_ctx *const_mach_to_ctx(const struct m0_sm *mach)
{
	return bob_of(mach, const struct m0_confc_ctx, fc_mach, &ctx_bob);
}

static struct m0_confc_ctx *ast_to_ctx(struct m0_sm_ast *ast)
{
	return bob_of(ast, struct m0_confc_ctx, fc_ast, &ctx_bob);
}

/* ------------------------------------------------------------------
 * AST
 * ------------------------------------------------------------------ */

static void _state_set(struct m0_sm_group *grp __attribute__((unused)),
		       struct m0_sm_ast *ast)
{
	int state = *(int32_t *)ast->sa_datum;
	M0_PRE(M0_IN(state, (S_INITIAL, S_CHECK, S_WAIT_REPLY, S_WAIT_STATUS,
			     S_GROW_CACHE, /* note the absence of S_FAILURE */
			     S_TERMINAL)));

	m0_sm_state_set(&ast_to_ctx(ast)->fc_mach, state);
}

static void _fail(struct m0_sm_group *grp __attribute__((unused)),
		  struct m0_sm_ast *ast)
{
	m0_sm_fail(&ast_to_ctx(ast)->fc_mach, S_FAILURE,
		   *(int32_t *)ast->sa_datum);
}

static void _ast_post(struct m0_sm_ast *ast,
		      void (*cb)(struct m0_sm_group *, struct m0_sm_ast *),
		      int32_t datum)
{
	struct m0_confc_ctx *ctx = ast_to_ctx(ast);

	ast->sa_cb = cb;
	M0_ASSERT(ast->sa_datum == &ctx->fc_ast_datum);
	ctx->fc_ast_datum = datum;

	m0_sm_ast_post(ctx->fc_confc->cc_group, ast);
}

/** Posts an AST that will advance the state machine to given state. */
static void ast_state_set(struct m0_sm_ast *ast, enum confc_ctx_state state)
{
	_ast_post(ast, _state_set, state);
}

/** Posts an AST that will move the state machine to S_FAILURE state. */
static void ast_fail(struct m0_sm_ast *ast, int32_t rc)
{
	_ast_post(ast, _fail, rc);
}

/* ------------------------------------------------------------------
 * Configuration cache management
 * ------------------------------------------------------------------ */

static int object_enrich(struct m0_conf_obj *dest,
			 const struct confx_object *src,
			 struct m0_conf_reg *reg);
static int cache_preload_locked(struct m0_confc *confc, const char *conf_str);

static struct m0_confc *registry_to_confc(struct m0_conf_reg *reg)
{
	return bob_of(reg, struct m0_confc, cc_registry, &confc_bob);
}

static int
cached_obj_update(struct m0_conf_reg *reg, const struct confx_object *flat)
{
	struct m0_conf_obj *obj;

	M0_PRE(confc_is_locked(registry_to_confc(reg)));

	return m0_conf_obj_find(reg, flat->o_conf.u_type, &flat->o_id, &obj) ?:
		object_enrich(obj, flat, reg);
}

/** Adds objects, described by a configuration string, to the cache. */
static int cache_preload(struct m0_confc *confc, const char *local_conf)
{
	int rc;

	M0_ENTRY();
	M0_PRE(confc->cc_root->co_status == M0_CS_MISSING);

	m0_mutex_lock(&confc->cc_lock);
	rc = cache_preload_locked(confc, local_conf);
	m0_mutex_unlock(&confc->cc_lock);
	if (rc != 0)
		M0_RETURN(rc);

	if (confc->cc_root->co_status == M0_CS_MISSING)
		M0_RETERR(-EBADF,
			  "Profile doesn't match pre-loaded configuration");

	M0_POST(confc->cc_root->co_status == M0_CS_READY);
	M0_RETURN(0);
}

static int cache_preload_locked(struct m0_confc *confc, const char *conf_str)
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

	M0_ENTRY();
	M0_PRE(confc_is_locked(confc));

	rc = nr_objs = m0_conf_parse(conf_str, objs, ARRAY_SIZE(objs));
	if (rc < 0)
		M0_RETURN(rc);

	for (i = 0, rc = 0; i < nr_objs && rc == 0; ++i)
		rc = cached_obj_update(&confc->cc_registry, &objs[i]);

	m0_confx_fini(objs, nr_objs);
	M0_RETURN(rc);
}

static int object_enrich(struct m0_conf_obj *dest,
			 const struct confx_object *src,
			 struct m0_conf_reg *reg)
{
	int              rc;
	struct m0_confc *confc = registry_to_confc(reg);

	M0_PRE(dest->co_type == src->o_conf.u_type);
	M0_PRE(confc_is_locked(confc));
	M0_PRE(dest->co_confc == NULL || dest->co_confc == confc);

	if (!m0_conf_obj_match(dest, src)) {
		M0_LOG(M0_WARN,
		       "Conflict of incoming and cached configuration data");
		return -EPROTO;
	}

	if (dest->co_status == M0_CS_READY)
		return 0; /* do nothing */

	if (dest->co_confc == NULL)
		dest->co_confc = confc;

	rc = m0_conf_obj_fill(dest, src, reg);
	if (rc != 0)
		dest->co_status = M0_CS_MISSING;
	m0_chan_broadcast(&dest->co_chan);

	return rc;
}

/**
 * Adds new objects, contained in confd's response, to the
 * configuration cache.
 *
 * @pre  resp->fr_rc == 0
 */
static int
cache_grow(struct m0_conf_reg *reg, const struct m0_conf_fetch_resp *resp)
{
	struct confx_object *flat;
	uint32_t             i;
	int                  rc;
	struct m0_confc     *confc = registry_to_confc(reg);

	M0_PRE(resp->fr_rc == 0);
	M0_PRE(conf_group_is_locked(confc));

	confc_lock(confc);
	for (i = 0; i < resp->fr_data.ec_nr; ++i) {
		flat = &resp->fr_data.ec_objs[i];

		if (!m0_buf_is_aimed(&flat->o_id)) {
			M0_LOG(M0_ERROR, "Invalid confx_object received");
			rc = -EPROTO;
			break;
		}

		rc = cached_obj_update(reg, flat);
		if (rc != 0)
			break;
	}
	confc_unlock(confc);
	return rc;
}

/* ------------------------------------------------------------------
 * Networking
 * ------------------------------------------------------------------ */

static bool online(const struct m0_confc *confc)
{
	return confc->cc_rpc_conn.c_rpc_machine != NULL;
}

static int connect_to_confd(struct m0_confc *confc, const char *confd_addr,
			    struct m0_rpc_machine *rpc_mach)
{
	struct m0_net_end_point *ep;
	int rc;
	enum {
		NR_SLOTS = 5, /* That many m0_confc_ctx's will be able
			       * to talk to confd simultaneously. */
		MAX_RPCS_IN_FLIGHT = 10 /* XXX Or should it be == NR_SLOTS? */
	};

	M0_ENTRY();
	M0_PRE(not_empty(confd_addr) && rpc_mach != NULL);

	rc = m0_net_end_point_create(&ep, &rpc_mach->rm_tm, confd_addr);
	if (rc != 0)
		M0_RETURN(rc);

	rc = m0_rpc_conn_create(&confc->cc_rpc_conn, ep, rpc_mach,
				MAX_RPCS_IN_FLIGHT, M0_TIME_NEVER);
	m0_net_end_point_put(ep);
	if (rc != 0)
		M0_RETURN(rc);

	rc = m0_rpc_session_create(&confc->cc_rpc_session, &confc->cc_rpc_conn,
				   NR_SLOTS, M0_TIME_NEVER);
	if (rc != 0)
		(void)m0_rpc_conn_destroy(&confc->cc_rpc_conn, M0_TIME_NEVER);

	M0_POST(online(confc));
	M0_RETURN(rc);
}

static void disconnect_from_confd(struct m0_confc *confc)
{
	M0_ENTRY();
	M0_PRE(online(confc));
	M0_PRE(confc->cc_rpc_session.s_conn == &confc->cc_rpc_conn);

	(void)m0_rpc_session_destroy(&confc->cc_rpc_session, M0_TIME_NEVER);
	(void)m0_rpc_conn_destroy(&confc->cc_rpc_conn, M0_TIME_NEVER);

	M0_LEAVE();
}

static bool request_is_valid(const struct m0_conf_fetch *req)
{
	return  req->f_origin.oi_type < M0_CO_NR &&
		m0_buf_is_aimed(&req->f_origin.oi_id) &&
		equi(req->f_path.ab_count == 0, req->f_path.ab_elems == NULL);
}

/**
 * Fills m0_conf_fetch structure.
 *
 * @param ctx  Configuration retrieval context.
 * @param org  Origin of the path being sent to confd.
 * @param ri   The position (in ctx->fc_path[]) that starts the path
 *             being sent to confd.
 */
static void
request_fill(struct m0_confc_ctx *ctx, const struct m0_conf_obj *org, size_t ri)
{
	uint32_t              len;
	struct m0_conf_fetch *req = &ctx->fc_req;
	const struct m0_buf  *path = &ctx->fc_path[ri];

	M0_PRE(ctx_invariant(ctx));
	M0_PRE(online(ctx->fc_confc));

	req->f_origin.oi_type = org->co_type;
	req->f_origin.oi_id = org->co_id;

	for (len = 0; !eop(&ctx->fc_path[ri + len]); ++len)
		; /* measure path length */
	req->f_path.ab_count = len;
	req->f_path.ab_elems = len == 0 ? NULL :
		(struct m0_buf *)path; /* strip const */

	M0_POST(request_is_valid(req));
}

static const struct m0_rpc_item_ops req_item_ops = {
	.rio_replied = on_replied,
};

static void req_fop_release(struct m0_ref *ref)
{
	/* Do nothing */
	M0_ASSERT(false); /* XXX Discuss with vvv */
}

static void req_fop_init(struct m0_confc_ctx *ctx)
{
	if (online(ctx->fc_confc)) {
		struct m0_rpc_item *item = &ctx->fc_fop.f_item;

		M0_SET0(&ctx->fc_req);
		/* FIXME. Passing m0_fop_release() is incorrect. */
		m0_fop_init(&ctx->fc_fop, &m0_conf_fetch_fopt, &ctx->fc_req,
			    req_fop_release);
		item->ri_ops = &req_item_ops;
		item->ri_session = &ctx->fc_confc->cc_rpc_session;
	}
}

static bool req_fop_invariant(const struct m0_confc_ctx *ctx)
{
	const struct m0_rpc_item *item = &ctx->fc_fop.f_item;

	return ergo(online(ctx->fc_confc),
		    item->ri_type == &m0_conf_fetch_fopt.ft_rpc_item_type &&
		    item->ri_ops != NULL &&
		    item->ri_ops->rio_replied == on_replied &&
		    item->ri_session == &ctx->fc_confc->cc_rpc_session &&
		    m0_fop_data((struct m0_fop *)&ctx->fc_fop) == &ctx->fc_req);
}

static void req_fop_fini(struct m0_confc_ctx *ctx)
{
	if (online(ctx->fc_confc)) {
		/*
		 * We cannot use m0_fop_fini():
		 * - the precondition of m0_rpc_item_fini() is not met;
		 * - m0_xcode_free() would fail.
		 *
		 * Below is what is left of m0_fop_fini().
		 */
		m0_addb_ctx_fini(&ctx->fc_fop.f_addb);
	}
}

/* ------------------------------------------------------------------
 * Locking
 * ------------------------------------------------------------------ */

static void conf_group_lock(const struct m0_confc *confc)
{
	m0_mutex_lock(&confc->cc_group->s_lock);
}

static void conf_group_unlock(const struct m0_confc *confc)
{
	m0_mutex_unlock(&confc->cc_group->s_lock);
}

static bool conf_group_is_locked(const struct m0_confc *confc)
{
	return m0_mutex_is_locked(&confc->cc_group->s_lock);
}

static void confc_lock(struct m0_confc *confc)
{
	m0_mutex_lock(&confc->cc_lock);
}

static void confc_unlock(struct m0_confc *confc)
{
	m0_mutex_unlock(&confc->cc_lock);
}

static bool confc_is_locked(const struct m0_confc *confc)
{
	return m0_mutex_is_locked(&confc->cc_lock);
}

/** @} confc_dlspec */
