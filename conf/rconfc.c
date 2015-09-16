/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Igor Vartanov
 * Original author: Egor Nikulenkov
 * Original creation date: 03-Mar-2015
 */

#define M0_TRACE_SUBSYSTEM   M0_TRACE_SUBSYS_CONF

#include "lib/trace.h"
#include "lib/tlist.h"
#include "lib/mutex.h"
#include "lib/memory.h"    /* M0_ALLOC_PTR, m0_free */
#include "lib/errno.h"
#include "lib/locality.h"  /* m0_locality0_get() */
#include "lib/string.h"
#include "lib/finject.h"   /* M0_FI_ENABLED */
#include "mero/magic.h"
#include "rm/rm.h"
#include "rm/rm_service.h" /* m0_rm_svc_rwlock_get */
#include "rpc/rpclib.h"    /* m0_rpc_client_connect */
#include "conf/cache.h"
#include "conf/obj_ops.h"  /* m0_conf_obj_find */
#include "conf/confc.h"
#include "conf/rconfc.h"
#include "conf/rconfc_internal.h"

/**
 * @page rconfc-lspec rconfc Internals
 *
 * - @ref rconfc-lspec-sm
 * - @ref rconfc-lspec-rlock
 * - @ref rconfc-lspec-elect
 * - @ref rconfc-lspec-confc
 * - @ref rconfc-lspec-gate
 *   - @ref rconfc-lspec-gate-check
 *   - @ref rconfc-lspec-gate-drain
 *   - @ref rconfc-lspec-gate-skip
 * - @ref rconfc_dlspec "Detailed Logical Specification"
 *
 * <hr> <!------------------------------------------------------------>
 * @section rconfc-lspec-sm Rconfc state machine
 *
 * @dot
 *  digraph rconfc_sm {
 *      node [fontsize=9];
 *      edge [fontsize=9];
 *      "RCS_INIT"              [shape=rect, style=filled, fillcolor=lightgrey];
 *      "RCS_STARTING"          [shape=rect, style=filled, fillcolor=lightgrey];
 *      "RCS_GET_RLOCK"         [shape=rect, style=filled, fillcolor=green];
 *      "RCS_VERSION_ELECT"     [shape=rect, style=filled, fillcolor=green];
 *      "RCS_IDLE"              [shape=rect, style=filled, fillcolor=cyan];
 *      "RCS_RLOCK_CONFLICT"    [shape=rect, style=filled, fillcolor=pink];
 *      "RCS_CONDUCTOR_DRAIN"   [shape=rect, style=filled, fillcolor=pink];
 *      "RCS_STOPPING"          [shape=rect, style=filled, fillcolor=dimgray];
 *      "RCS_FAILURE"           [shape=rect, style=filled, fillcolor=tomato];
 *      "RCS_FINAL"             [shape=rect, style=filled, fillcolor=red];
 *
 *      "RCS_INIT" -> "RCS_STARTING"
 *      "RCS_STARTING" -> "RCS_GET_RLOCK"
 *      "RCS_STARTING" -> "RCS_FAILURE"
 *      "RCS_GET_RLOCK" -> "RCS_VERSION_ELECT"
 *      "RCS_GET_RLOCK" -> "RCS_FAILURE"
 *      "RCS_VERSION_ELECT" -> "RCS_IDLE"
 *      "RCS_VERSION_ELECT" -> "RCS_FAILURE"
 *      "RCS_IDLE" -> "RCS_RLOCK_CONFLICT"
 *      "RCS_IDLE" -> "RCS_STOPPING"
 *      "RCS_RLOCK_CONFLICT" -> "RCS_CONDUCTOR_DRAIN"
 *      "RCS_CONDUCTOR_DRAIN" -> "RCS_GET_RLOCK"
 *      "RCS_CONDUCTOR_DRAIN" -> "RCS_FAILURE"
 *      "RCS_STOPPING" -> "RCS_FINAL"
 *      "RCS_FAILURE" -> "RCS_STOPPING"
 *  }
 * @enddot
 *
 * Color agenda:                                          @n
 * @b light @b grey - Initialisation and startup states   @n
 * @b green         - States during startup or reelection @n
 * @b pink          - Reelection-only states              @n
 * @b dark @b grey  - Stopping states                     @n
 *
 * After successful start rconfc is in RCS_IDLE state, waiting for one of two
 * events: read lock conflict or user request for stopping. These two events
 * are handled only when rconfc is in RCS_IDLE state. If rconfc was in other
 * state, then a fact of the happened event is stored, but its handling is
 * delayed until rconfc state is RCS_IDLE.
 *
 * If failure is occured that prevents rconfc from functioning properly, then
 * rconfc goes to RCS_FAILURE state. SM in this state do nothing until user
 * requests for stopping.
 *
 * Rconfc internal state is protected by SM group lock. SM group is provided by
 * user on rconfc initialisation.
 *
 * <hr> <!------------------------------------------------------------>
 * @section rconfc-lspec-rlock Read Lock Acquisition and Revocation
 *
 * During m0_rconfc_start() execution rconfc requesting read lock from Resource
 * Manager (RM) by calling read_lock_get_st_in(). On request completion
 * rconfc_read_lock_complete() is called. Successful lock acquisition indicates
 * no configuration change is in progress and configuration reading is allowed.
 *
 * The read lock is retained by rconfc instance until finalisation. But the lock
 * can be revoked by RM in case a conflicting lock is requested. On the lock
 * revocation rconfc_read_lock_conflict() is called. The call installs
 * m0_confc_gate_ops::go_drain() callback to be notified when the last reading
 * context is detached from m0_rconfc::rc_confc instance. The callback ends in
 * calling rconfc_gate_drain() where rconfc starts conductor cache drain.
 * In conductor_drained_st_in() rconfc eventually puts the read lock back to RM.
 *
 * Being informed about the conflict, rconfc disallows configuration reading
 * done via m0_rconfc::rc_confc until the next read lock acquisition is
 * complete. Besides, in conductor_drain_check_st_in() the mentioned confc's
 * cache is drained to prevent consumer from reading cached-but-outdated
 * configuration values. However, the cache data remains untouched and readable
 * to the very moment when there is no cache object pinned anymore, and the last
 * reading context detaches from the confc being in use.
 *
 * When done with the cache, m0_rconfc::rc_confc is disconnected from confd
 * server to prevent unauthorized read operations. Then the conflicting lock is
 * returned back to RM complying with the conflict request.
 *
 * Immediately after revocation rconfc attempts to acquire read lock again. The
 * lock will be granted once the conflicting lock is released.
 *
 * <hr> <!------------------------------------------------------------>
 * @section rconfc-lspec-elect Version Election and Quorum
 *
 * In the course of rconfc_read_lock_complete() under condition of successful
 * read lock acquisition version_elect_st_in() is called. The call initialises
 * every confc instance of the m0_rconfc::rc_herd list, attaches
 * rconfc__cb_quorum_test() to its context and initiates asynchronous reading
 * from the corresponding confd server. When version quorum is either
 * reached or found impossible rconfc_version_elected() is called.
 *
 * On every reading event rconfc__cb_quorum_test() is called. In case the
 * reading context is not completed, the function returns zero value indicating
 * the process to go on. Otherwise rconfc_quorum_test() is called to see if
 * quorum is reached with the last reply. If quorum is reached or impossible,
 * then rconfc_version_elected() is called.
 *
 * Quorum is considered reached when the number of confd servers reported the
 * same version number is greater or equal to the value provided to
 * m0_rconfc_init(). In case zero value was provided, the required quorum number
 * is automatically calculated as a half of confd server count plus one.
 *
 * When quorum is reached, rconfc_conductor_engage() is called connecting
 * m0_rconfc::rc_confc with a confd server from active list. Starting from this
 * moment configuration reading is allowed until read lock is revoked.
 *
 * <hr> <!------------------------------------------------------------>
 * @section rconfc-lspec-gate Gating confc operations
 *
 * @subsection rconfc-lspec-gate-check Blocking confc context initialisation
 *
 * Rconfc performs gating read operations conducted through the confc instance
 * governed by the rconfc, i.e. m0_rconfc::rc_confc. When read lock is acquired
 * by rconfc, the reading is allowed. To be allowed to go on with reading,
 * m0_confc_ctx_init() performs checking by calling previously set callback
 * m0_confc::cc_gops::go_check(), that in fact is rconfc_gate_check().
 *
 * With the read lock revoked inside rconfc_gate_check() rconfc blocks any
 * m0_confc_ctx_init() calls done with this particular m0_rconfc::rc_confc. On
 * next successful read lock acquisition all the previously blocked contexts get
 * unblocked. Once being allowed to read, the context can be used as many times
 * as required.
 *

   @msc
   wordwraparcs="1", hscale="2.0";
   rm[ label = RM ],
   rc[ label = rconfc, linecolor="#0000ff", textcolor="#0000ff" ],
   c [ label = "rconfc->rc_confc", linecolor="#0000ff", textcolor="#0000ff" ],
   x [ label = confc_ctx ],
   m [ label = consumer ];

   ||| ;
   rm note rm [ label = "grant read lock", textcolor="#00aa00" ];
   rm=>>rc [ label = "rconfc_read_lock_complete(rc = 0)", textcolor="#00aa00"];
   rc=>rc  [ label = "state = RCS_VERSION_ELECT" ];
   ---     [ label = "waiting until version is elected", linecolor="#0000ff",
   textcolor="#0000ff"];
   rc=>rc  [ label = "rconfc_conductor_engage" ];
   rc=>rc  [ label = "state = RCS_IDLE" ];
   rc note rc [ label = "reading allowed = true" ];
   ... ;
   m=>x   [ label = "m0_confc_ctx_init"];
   x=>c   [ label = "get .go_check"];
   x<<c   [ label = ".go_check"];
   ---    [ label = "if (.go_check != NULL)" ];
   x=>>rc [ label = "rconfc_gate_check" ];
   rc>>x  [ label = "allowed = true" ];
   ---    [ label = "else // reading allowed by default" ];
   x note x [ label = "fc_allowed = true" ];
   x>>m   [ label = "return from init" ];
   m=>x   [ label = "m0_confc_open_sync(...)" ];
   x=>x   [ label = "test if fc_allowed is true" ];
   ... ;
   x>>m   [ label = "return" ];
   ||| ;

   @endmsc

 * <br/><center>
 * @b Diag.1: @b "Reading allowed at the moment of context initialisation"
 * </center><br/>

   @msc
   wordwraparcs="1", hscale="2.0";
   rm[ label = RM ],
   rc[ label = rconfc, linecolor="#0000ff", textcolor="#0000ff" ],
   c [ label = "rconfc->rc_confc", linecolor="#0000ff", textcolor="#0000ff" ],
   x [ label = confc_ctx ],
   m [ label = consumer ];

   ||| ;
   rm note rm [ label = "revoke read lock", textcolor="#cc0000" ];
   rm=>>rc [ label = "rconfc_read_lock_conflict", textcolor="#cc0000"];
   rc note rc [ label = "reading allowed = false" ];
   rc=>rc  [ label = ".go_drain = rconfc_gate_drain" ];
   x<=m    [ label = "m0_confc_ctx_fini" ];
   rc<<=x  [ label = "rconfc_gate_drain" ];
   rc=>rc  [ label = "wait till all conf objects unpinned" ];
   rc=>c   [ label = "m0_confc_reconnect(NULL)" ];
   rm<=rc  [ label = "rconfc_read_lock_put" ];
   rm<=rc  [ label = "get read lock" ];
   rc=>rc  [ label = "state = RCS_GET_RLOCK" ];
   ... ;
   m=>x   [ label = "m0_confc_ctx_init"];
   x=>c   [ label = "get .go_check"];
   x<<c   [ label = ".go_check"];
   ---    [ label = "if (.go_check != NULL)" ];
   x=>>rc [ label = "rconfc_gate_check"];
   rc=>rc [ label = "wait until rconfc in (RCS_IDLE, RCS_FAILURE)" ];
   ---    [ label = "waiting in rconfc_gate_check", linecolor="#0000ff",
   textcolor="#0000ff"];
   ... ;
   rm note rm [ label = "grant read lock", textcolor="#00aa00" ];
   rm=>>rc [ label = "rconfc_read_lock_complete(rc = 0)", textcolor="#00aa00"];
   rc=>rc  [ label = "state = RCS_VERSION_ELECT" ];
   ---     [ label = "waiting until version is elected", linecolor="#00aa00",
   textcolor="#00aa00"];
   rc=>rc  [ label = "rconfc_conductor_engage" ];
   rc=>rc  [ label = "state = RCS_IDLE" ];
   rc note rc [ label = "reading allowed = true" ];
   ---    [ label = "waking up in rconfc_gate_check", linecolor="#0000ff",
   textcolor="#0000ff"];
   rc>>x  [ label = "reading allowed = true" ];
   x note x [ label = "fc_allowed = true" ];
   x>>m   [ label = "return from init" ];
   m=>x   [ label = "m0_confc_open_sync(...)" ];
   x=>x   [ label = "test if fc_allowed is true" ];
   ... ;
   x>>m   [ label = "return success" ];
   ||| ;

   @endmsc

 * <br/><center>
 * @b Diag.2: @b "Reading disallowed at the moment of context initialisation"
 * </center><br/>

   @msc
   wordwraparcs="1", hscale="2.0";
   rm[ label = RM ],
   rc[ label = rconfc, linecolor="#0000ff", textcolor="#0000ff" ],
   c [ label = "rconfc->rc_confc", linecolor="#0000ff", textcolor="#0000ff" ],
   x [ label = confc_ctx ],
   m [ label = consumer ];

   ||| ;
   rm note rm [ label = "revoke read lock", textcolor="#cc0000" ];
   rm=>>rc [ label = "rconfc_read_lock_conflict", textcolor="#cc0000"];
   rc note rc [ label = "reading allowed = false" ];
   rc=>rc [ label = ".go_drain = rconfc_gate_drain" ];
   x<=m   [ label = "m0_confc_ctx_fini" ];
   rc<<=x [ label = "rconfc_gate_drain" ];
   rc=>rc [ label = "drain cache" ];
   rc=>c  [ label = "m0_confc_reconnect(NULL)" ];
   rm<=rc [ label = "rconfc_read_lock_put" ];
   rm<=rc [ label = "get read lock" ];
   rc=>rc [ label = "state = RCS_GET_RLOCK" ];
   ... ;
   m=>x   [ label = "m0_confc_ctx_init"];
   x=>c   [ label = "get .go_check"];
   x<<c   [ label = ".go_check"];
   ---    [ label = "if (.go_check != NULL)" ];
   x=>>rc [ label = "rconfc_gate_check"];
   rc=>rc [ label = "wait until rconfc in (RCS_IDLE, RCS_FAILURE)" ];
   ---    [ label = "waiting in rconfc_gate_check", linecolor="#0000ff",
   textcolor="#0000ff"];
   ... ;
   rm note rc [ label = "communication failed", textcolor="#cc0000" ];
   rm=>>rc [ label = "rconfc_read_lock_complete(rc != 0)", textcolor="#cc0000"];
   rc=>rc  [ label = "state = RCS_FAILURE"];
   rc note rc [ label = "reading allowed = false" ];
   ---    [ label = "waking up in rconfc_gate_check", linecolor="#0000ff",
   textcolor="#0000ff"];
   rc>>x  [ label = "reading allowed = false" ];
   x note x [ label = "fc_allowed = false" ];
   x>>m   [ label = "return from init" ];
   m=>x   [ label = "m0_confc_open_sync(...)" ];
   x=>x   [ label = "test if fc_allowed is true" ];
   x>>m   [ label = "return -EINVAL" ];
   ... ;
   ||| ;

   @endmsc

 * <br/><center>
 * @b Diag.3: @b "Reading remains disallowed because of
 *                RM communication failure"
 * </center><br/>
 *
 * @subsection rconfc-lspec-gate-drain Cleaning confc cache data
 *
 * When new configuration change is in progress, and therefore, read lock is
 * revoked, rconfc_read_lock_conflict() defers cache draining until there is no
 * reading context attached. It installs m0_confc::cc_gops::go_drain() callback,
 * that normally remains set to NULL and this way does not affect execution of
 * m0_confc_ctx_fini() anyhow. But with the callback set up, at the moment of
 * the very last detach m0_confc_ctx_fini() calls m0_confc::cc_gops::go_drain()
 * callback, that in fact is rconfc_gate_drain(), where cache cleanup is finally
 * invoked by setting RCS_CONDUCTOR_DRAIN state. Rconfc SM remains in
 * RCS_CONDUCTOR_DRAIN_CHECK state until all conf objects are unpinned. Once
 * there are no pinned objects, rconfc cleans cache, put read lock and starts
 * reelection process.
 *

   @msc
   wordwraparcs="1", hscale="2.0";
   rm[ label = RM ],
   rc[ label = rconfc, linecolor="#0000ff", textcolor="#0000ff" ],
   c [ label = "rconfc->rc_confc", linecolor="#0000ff", textcolor="#0000ff" ],
   h [ label = confc_cache ],
   x [ label = confc_ctx ],
   m [ label = consumer ];

   ||| ;
   rm note rm [ label = "revoke read lock", textcolor="#cc0000" ];
   rm=>>rc [ label = "rconfc_read_lock_conflict", textcolor="#cc0000"];
   rc=>c   [ label = "get number of contexts attached" ];
   rc<<c   [ label = " number of contexts attached" ];
   ---     [ label = " if no context attached ", linecolor="#0000ff",
   textcolor="#0000ff" ];
   rc=>rc  [ label = "rconfc_gate_drain" ];
   rc=>rc  [ label = "state = RCS_CONDUCTOR_DRAIN" ];
   ---     [ label = " else if any context(s) attached ", linecolor="#0000ff",
   textcolor="#0000ff" ];
   rc=>rc  [ label = " .go_drain = rconfc_gate_drain" ];
   ... ;
   x<=m    [ label = "m0_confc_ctx_fini" ];
   x=>x    [ label = "M0_CNT_DEC(confc->cc_nr_ctx)" ];
   ---     [ label = " on no context attached " ];
   x=>c    [ label = "get .go_drain" ];
   c>>x    [ label = " .go_drain " ];
   rc<<=x  [ label = "rconfc_gate_drain" ];
   rc=>rc  [ label = " .go_drain = NULL" ];
   rc>>x   [ label = "return" ];
   rc=>rc  [ label = "state = RCS_CONDUCTOR_DRAIN" ];
   ---     [ label = " loop until no pinned object remains ",
   linecolor="#00aaaa", textcolor="#00aaaa" ];
   rc=>h   [ label = "find first pinned object" ];
   h>>rc   [ label = "return m0_conf_obj" ];
   rc=>rc  [ label = "wait for object closure" ];
   ... ;
   m=>h    [ label = "m0_confc_close(waited conf object)" ];
   h->rc   [ label = "broadcast obj->co_chan" ];
   h>>m    [ label = "return" ];
   ---     [ label = "set RCS_CONDUCTOR_DRAIN and search for a
   pinned object ", linecolor="#00aaaa", textcolor="#00aaaa" ];
   rc=>h   [ label = "m0_conf_cache_clean" ];
   h=>h    [ label = "delete all cached objects" ];
   h>>rc   [ label = "return" ];
   ... ;
   ||| ;

   @endmsc

 * <br/><center>
 * @b Diag.4: @b "Deferred Cache Cleanup"
 * </center><br/>
 *
 * @note Forced cache draining occurs when m0_confc_gate_ops::go_drain callback
 * is installed, which happens only when reading is not allowed. Normally the
 * callback is set to NULL, and therefore, confc cache remains unaffected during
 * m0_confc_ctx_fini().
 *
 * @subsection rconfc-lspec-gate-skip Reconnecting confc to another confd
 *
 * In case configuration reading fails because of network error, the confc
 * context requests the confc to skip its current connection to confd and switch
 * to some other confd server running the same version. This is done inside
 * state machine being in S_SKIP_CONFD state by calling callback function
 * m0_confc::cc_gops::go_skip() that in fact is rconfc_gate_skip(). The function
 * iterates through the m0_rconfc::rc_active list and returns on the first
 * successful connection established. In case of no success, the function
 * returns with -ENOENT making the state machine end in S_FAILURE state.
 *
 * @note As long as confc is switched to confd of the same version number, the
 * cache data remains valid and needs no special attendance.
 *
 */

/**
 * @defgroup rconfc_dlspec rconfc Internals
 *
 * @{
 */

static bool rconfc_gate_check(struct m0_confc *confc);
static int  rconfc_gate_skip(struct m0_confc *confc);
static bool rconfc_gate_drain(struct m0_clink *clink);

struct m0_confc_gate_ops m0_rconfc_gate_ops = {
	.go_check = rconfc_gate_check,
	.go_skip  = rconfc_gate_skip,
	.go_drain = rconfc_gate_drain,
};

static void rconfc_read_lock_complete(struct m0_rm_incoming *in, int32_t rc);
static void rconfc_read_lock_conflict(struct m0_rm_incoming *in);

struct m0_rm_incoming_ops m0_rconfc_ri_ops = {
	.rio_complete = rconfc_read_lock_complete,
	.rio_conflict = rconfc_read_lock_conflict,
};

static struct m0_sm_state_descr rconfc_states[] = {
	[RCS_INIT] = {
		.sd_flags     = M0_SDF_INITIAL,
		.sd_name      = "RCS_INIT",
		.sd_allowed   = M0_BITS(RCS_STARTING)
	},
	[RCS_STARTING] = {
		.sd_name      = "RCS_STARTING",
		.sd_allowed   = M0_BITS(RCS_GET_RLOCK, RCS_FAILURE),
	},
	[RCS_GET_RLOCK] = {
		.sd_name      = "RCS_GET_RLOCK",
		.sd_allowed   = M0_BITS(RCS_VERSION_ELECT, RCS_FAILURE),
	},
	[RCS_VERSION_ELECT] = {
		.sd_name      = "RCS_VERSION_ELECT",
		.sd_allowed   = M0_BITS(RCS_IDLE, RCS_FAILURE),
	},
	[RCS_IDLE] = {
		.sd_name      = "RCS_IDLE",
		.sd_allowed   = M0_BITS(RCS_STOPPING, RCS_RLOCK_CONFLICT),
	},
	[RCS_RLOCK_CONFLICT] = {
		.sd_name      = "RCS_RLOCK_CONFLICT",
		.sd_allowed   = M0_BITS(RCS_CONDUCTOR_DRAIN),
	},
	[RCS_CONDUCTOR_DRAIN] = {
		.sd_name      = "RCS_CONDUCTOR_DRAIN",
		.sd_allowed   = M0_BITS(RCS_GET_RLOCK, RCS_FAILURE),
	},
	[RCS_STOPPING] = {
		.sd_name      = "RCS_STOPPING",
		.sd_allowed   = M0_BITS(RCS_FINAL),
	},
	[RCS_FAILURE] = {
		.sd_flags     = M0_SDF_FAILURE,
		.sd_name      = "RCS_FAILURE",
		.sd_allowed   = M0_BITS(RCS_STOPPING)
	},
	[RCS_FINAL] = {
		.sd_flags     = M0_SDF_TERMINAL,
		.sd_name      = "RCS_FINAL"
	}
};

static const struct m0_sm_conf rconfc_sm_conf = {
	.scf_name      = "Rconfc",
	.scf_nr_states = ARRAY_SIZE(rconfc_states),
	.scf_state     = rconfc_states
};

/***************************************
 * List Definitions
 ***************************************/

M0_TL_DESCR_DEFINE(rcnf_herd, "rconfc's working  confc list", M0_INTERNAL,
		   struct rconfc_link, rl_herd, rl_magic,
		   M0_RCONFC_LINK_MAGIC, M0_RCONFC_HERD_HEAD_MAGIC
	);
M0_TL_DEFINE(rcnf_herd, M0_INTERNAL, struct rconfc_link);

M0_TL_DESCR_DEFINE(rcnf_active, "rconfc's active confc list", M0_INTERNAL,
		   struct rconfc_link, rl_active, rl_magic,
		   M0_RCONFC_LINK_MAGIC, M0_RCONFC_ACTIVE_HEAD_MAGIC
	);
M0_TL_DEFINE(rcnf_active, M0_INTERNAL, struct rconfc_link);

/***************************************
 * Helpers
 ***************************************/

/* -------------- Read lock context ----------------- */

static struct m0_rconfc *rlock_ctx_incoming_to_rconfc(struct m0_rm_incoming *in)
{
	struct rlock_ctx *rlx;

	rlx = container_of(in, struct rlock_ctx, rlc_req);
	return rlx->rlc_parent;
}

static void rlock_ctx_disconnect(struct rlock_ctx *rlx)
{
	int rc;

	M0_PRE(rlx->rlc_online);
	rc = m0_rpc_session_destroy(&rlx->rlc_sess, M0_TIME_NEVER);
	if (rc != 0)
		M0_LOG(M0_ERROR, "Failed to destroy rlock session");
	rc = m0_rpc_conn_destroy(&rlx->rlc_conn, M0_TIME_NEVER);
	if (rc != 0)
		M0_LOG(M0_ERROR, "Failed to destroy rlock connection");
	rlx->rlc_online = false;
}

static int rlock_ctx_connect(struct rlock_ctx *rlx)
{
	enum { MAX_RPCS_IN_FLIGHT = 15 };

	int rc;

	M0_ENTRY();
	M0_PRE(!rlx->rlc_online);
	if (M0_FI_ENABLED("rm_conn_failed"))
		rc = M0_ERR(-ECONNREFUSED);
	else
		rc = m0_rpc_client_connect(&rlx->rlc_conn, &rlx->rlc_sess,
					   rlx->rlc_rmach, rlx->rlc_rm_addr,
					   MAX_RPCS_IN_FLIGHT);
	rlx->rlc_online = (rc == 0);
	return M0_RC(rc);
}

static bool rlock_ctx_is_initialised(const struct rlock_ctx *rlx)
{
	return rlx->rlc_parent != NULL;
}

static int rlock_ctx_init(struct rlock_ctx      *rlx,
			  struct m0_rconfc      *parent,
			  struct m0_rpc_machine *rmach,
			  const char            *rm_addr)
{
	M0_ENTRY();
	M0_PRE(rlx != NULL);
	M0_PRE(rmach != NULL);
	M0_PRE(rm_addr != NULL);
	M0_PRE(M0_IS0(rlx));

	rlx->rlc_parent = parent;
	rlx->rlc_rmach = rmach;
	rlx->rlc_rm_addr = m0_strdup(rm_addr);
	if (rlx->rlc_rm_addr == NULL)
		return M0_ERR(-ENOMEM);

	return M0_RC(0);
}

static void rlock_ctx_owner_setup(struct rlock_ctx *rlx)
{
	struct m0_rm_owner  *owner;
	struct m0_rm_remote *creditor;

	m0_rw_lockable_init(&rlx->rlc_rwlock, &M0_RWLOCK_FID,
			    m0_rwlockable_domain());
	owner            = &rlx->rlc_owner;
	creditor         = &rlx->rlc_creditor;
	m0_fid_tgenerate(&rlx->rlc_owner_fid, M0_RM_OWNER_FT);
	m0_rm_rwlock_owner_init(owner, &rlx->rlc_owner_fid,
				&rlx->rlc_rwlock, NULL);
	m0_rm_remote_init(creditor, owner->ro_resource);
	creditor->rem_session = &rlx->rlc_sess;
	owner->ro_creditor    = creditor;
}

static void rlock_ctx_owner_windup(struct rlock_ctx *rlx)
{
	int rc;

	if (rlx->rlc_creditor.rem_session != NULL) {
		m0_rm_owner_windup(&rlx->rlc_owner);
		rc = m0_rm_owner_timedwait(&rlx->rlc_owner,
					   M0_BITS(ROS_FINAL, ROS_INSOLVENT),
					   M0_TIME_NEVER);
		M0_ASSERT(rc == 0);
		m0_rm_remote_fini(&rlx->rlc_creditor);
		m0_rm_rwlock_owner_fini(&rlx->rlc_owner);
		m0_rw_lockable_fini(&rlx->rlc_rwlock);
	}
}

static void rlock_ctx_fini(struct rlock_ctx *rlx)
{
	m0_free0(&rlx->rlc_rm_addr);
	M0_SET0(rlx);
}

/* -------------- Quorum calculation context ----------------- */

static int ver_accm_alloc_init(struct ver_accm **va, int total)
{
	M0_ENTRY();
	M0_PRE(total <= VERSION_ITEMS_TOTAL_MAX);
	M0_ALLOC_PTR(*va);
	if (*va == NULL)
		return M0_ERR(-ENOMEM);
	M0_SET0(*va);
	(*va)->va_total = total;
	return M0_RC(0);
}

/* -------------- Confc Helpers -------------- */

static uint64_t _confc_ver_read(const struct m0_confc *confc)
{
	return confc->cc_cache.ca_ver;
}

static bool _confc_is_online(const struct m0_confc *confc)
{
	return confc->cc_rpc_conn.c_rpc_machine != NULL;
}

static const char *_confc_remote_addr_read(const struct m0_confc *confc)
{
	return _confc_is_online(confc) ?
		m0_rpc_conn_addr(&confc->cc_rpc_conn) : NULL;
}

static bool _confc_is_inited(struct m0_confc *confc)
{
	return confc->cc_group != NULL;
}

static int _confc_cache_clean(struct m0_confc *confc)
{
	struct m0_conf_cache *cache = &confc->cc_cache;

	M0_ENTRY();
	m0_conf_cache_clean(cache);
	/* Clear version to prevent version mismatch error after reelection */
	cache->ca_ver = M0_CONF_VER_UNKNOWN;
	/**
	 * @todo Confc root pointer is not valid anymore after cache cleanup, so
	 * it should be reinitialised. The easiest way would be to reinitialise
	 * confc completely, but user can create confc contexts during
	 * reelection, so let's reinitialise root object the hackish way.
	 */
	return M0_RC(m0_conf_obj_find(cache, &M0_CONF_ROOT_FID,
				      &confc->cc_root));
}

/* -------------- Rconfc Helpers -------------- */

static uint32_t rconfc_quorum_default_calc(uint32_t total)
{
	return total/2 + 1;
}

static uint32_t rconfc_confd_count(const char **confd_addr)
{
	uint32_t count;

	M0_PRE(confd_addr != NULL);
	count = 0;
	while (*confd_addr++ != NULL)
		++count;
	return count;
}

static bool rconfc_confd_are_all_unique(const char **confd_addr)
{
	uint32_t count = rconfc_confd_count(confd_addr);
	return m0_forall(i, count,
			 m0_forall(j, count,
				   i == j ? true :
				   !m0_streq(confd_addr[i],
					     confd_addr[j])));
}

/***************************************
 * Rconfc private part
 ***************************************/

static bool rconfc_is_locked(struct m0_rconfc *rconfc)
{
	return m0_sm_group_is_locked(rconfc->rc_sm.sm_grp);
}

/** Read Lock cancellation */
static void rconfc_read_lock_put(struct m0_rconfc *rconfc)
{
	struct rlock_ctx *rlx;

	M0_ENTRY("rconfc = %p", rconfc);
	rlx = rconfc->rc_rlock_ctx;
	if (!M0_IS0(&rlx->rlc_req)) {
	    if (!M0_IN(rlx->rlc_req.rin_sm.sm_state,
		       (RI_INITIALISED, RI_FAILURE, RI_RELEASED)))
			m0_rm_credit_put(&rlx->rlc_req);
		m0_rm_rwlock_req_fini(&rlx->rlc_req);
	}
	M0_SET0(&rlx->rlc_req);
	M0_LEAVE();
}

static void rconfc_ast_post(struct m0_rconfc  *rconfc,
			    void             (*cb)(struct m0_sm_group *,
				                   struct m0_sm_ast *))
{
	struct m0_sm_ast *ast = &rconfc->rc_state_ast;

	ast->sa_cb = cb;
	ast->sa_datum = rconfc;
	m0_sm_ast_post(rconfc->rc_sm.sm_grp, ast);
}

static void rconfc_state_set(struct m0_rconfc *rconfc, int state)
{
        M0_LOG(M0_DEBUG, "rconfc: %p, state change:[%s -> %s]",
	       rconfc, m0_sm_state_name(&rconfc->rc_sm, rconfc->rc_sm.sm_state),
	       m0_sm_state_name(&rconfc->rc_sm, state));

	m0_sm_state_set(&rconfc->rc_sm, state);
}

static void rconfc_fail(struct m0_rconfc *rconfc, int rc)
{
        M0_LOG(M0_ERROR, "rconfc: %p, state %s failed with %d", rconfc,
	       m0_sm_state_name(&rconfc->rc_sm, rconfc->rc_sm.sm_state), rc);
	/*
	 * Put read lock on failure, because this rconfc can prevent remote
	 * write lock requests from completion.
	 */
	rconfc_read_lock_put(rconfc);
	m0_sm_fail(&rconfc->rc_sm, RCS_FAILURE, rc);
}

static void _failure_ast_cb(struct m0_sm_group *grp M0_UNUSED,
			    struct m0_sm_ast   *ast)
{
	struct m0_rconfc *rconfc = ast->sa_datum;

	rconfc_fail(rconfc, rconfc->rc_datum);
}

static void rconfc_fail_ast(struct m0_rconfc *rconfc, int rc)
{
	rconfc->rc_datum = rc;
	rconfc_ast_post(rconfc, _failure_ast_cb);
}

static bool rconfc_reading_is_allowed(const struct m0_rconfc *rconfc)
{
	M0_PRE(rconfc != NULL);
	return rconfc->rc_sm.sm_state == RCS_IDLE;
}

static bool rconfc_quorum_is_reached(struct m0_rconfc *rconfc)
{
	M0_PRE(rconfc_is_locked(rconfc));
	return rconfc->rc_ver != M0_CONF_VER_UNKNOWN;
}

static void rconfc_active_all_unlink(struct m0_rconfc *rconfc)
{
	struct rconfc_link *lnk;

	/* unlink all active entries */
	m0_tl_teardown(rcnf_active, &rconfc->rc_active, lnk) {
		rcnf_active_tlink_fini(lnk);
	}
}

static void rconfc_herd_init(struct m0_rconfc *rconfc)
{
	struct rconfc_link *lnk;

	m0_tl_for(rcnf_herd, &rconfc->rc_herd, lnk) {
		lnk->rl_rc = m0_confc_init(lnk->rl_confc, rconfc->rc_sm.sm_grp,
					   lnk->rl_confd_addr, rconfc->rc_rmach,
					   NULL);
		if (lnk->rl_rc == 0)
			lnk->rl_state = CONFC_IDLE;
	} m0_tl_endfor;
}

static void rconfc_herd_fini(struct m0_rconfc *rconfc)
{
	struct rconfc_link *lnk;

	m0_tl_for(rcnf_herd, &rconfc->rc_herd, lnk) {
		if (lnk->rl_state != CONFC_DEAD) {
			m0_clink_del(&lnk->rl_clink);
			m0_clink_fini(&lnk->rl_clink);
			m0_confc_ctx_fini_locked(&lnk->rl_cctx);
			m0_confc_fini(lnk->rl_confc);
		}
		/* dead confc has no internals to fini */
	} m0_tl_endfor;
}

static void rconfc_herd_prune(struct m0_rconfc *rconfc)
{
	struct rconfc_link *lnk;

	m0_tl_teardown(rcnf_herd, &rconfc->rc_herd, lnk) {
		rcnf_herd_tlink_fini(lnk);
		m0_free(lnk->rl_confc);
		m0_free(lnk->rl_confd_addr);
		m0_free(lnk);
	}
}

/**
 * Re-populates the active list based on the herd items current
 * status. Population starts when quorum version is found.
 */
static void rconfc_active_populate(struct m0_rconfc *rconfc)
{
	struct rconfc_link *lnk;

	M0_PRE(rconfc->rc_ver != M0_CONF_VER_UNKNOWN);
	rconfc_active_all_unlink(rconfc);
	/* re-populate active list */
	m0_tl_for(rcnf_herd, &rconfc->rc_herd, lnk) {
		if (lnk->rl_state == CONFC_OPEN &&
		    _confc_ver_read(lnk->rl_confc) == rconfc->rc_ver)
			rcnf_active_tlink_init_at_tail(lnk, &rconfc->rc_active);
	} m0_tl_endfor;
}

/**
 * Connects "conductor" confc to the confd server identified by the provided
 * link. Initialises the confc in case it was not done before.
 */
static int rconfc_conductor_connect(struct m0_rconfc   *rconfc,
				    struct rconfc_link *lnk)
{
	M0_ENTRY("rconfc = %p, lnk = %p, confd_addr = %s", rconfc, lnk,
		 lnk != NULL ? lnk->rl_confd_addr : "(null)");
	M0_PRE(rconfc != NULL);
	M0_PRE(lnk    != NULL);
	if (!_confc_is_inited(&rconfc->rc_confc)) {
		int rc;
		/* first use, initialization required */
		M0_PRE(_confc_ver_read(&rconfc->rc_confc) ==
		       M0_CONF_VER_UNKNOWN);
		rc = m0_confc_init(&rconfc->rc_confc, rconfc->rc_sm.sm_grp,
				   lnk->rl_confd_addr, rconfc->rc_rmach, NULL);
		if (rc != 0)
			return M0_ERR(rc);
		m0_confc_gate_ops_set(&rconfc->rc_confc, &rconfc->rc_gops);
	}
	return M0_RC(m0_confc_reconnect(&rconfc->rc_confc, rconfc->rc_rmach,
					lnk->rl_confd_addr));
}

/**
 * Iterates through active list entries and tries to connect next confd
 * address. Finishes with either connection succeeded or list exhausted.
 *
 * The intended effect is that all spare confc items are going to be marked
 * CONFC_IDLE with the following exceptions:
 * - the newly connected item is marked CONFC_OPEN
 * - the previously connected item, i.e. the one which failure led to the
 * iteration, is marked CONFC_FAILED
 * - every item found non-responsive during the iteration is marked
 * CONFC_FAILED
 *
 * @note All CONFC_FAILED items are going to be re-set to CONFC_IDLE, and due to
 * this, re-tried when next iteration starts.
 */
static int rconfc_conductor_iterate(struct m0_rconfc *rconfc)
{
	struct rconfc_link *next;
	struct rconfc_link *prev;
	const char         *confd_addr;
	int                 rc;

	M0_ENTRY("rconfc = %p", rconfc);
	M0_PRE(rconfc != NULL);
	M0_PRE(rconfc->rc_ver != M0_CONF_VER_UNKNOWN);
	confd_addr = _confc_remote_addr_read(&rconfc->rc_confc) ?: "";
	M0_PRE((prev = m0_tl_find(rcnf_active, item, &rconfc->rc_active,
				  m0_streq(confd_addr, item->rl_confd_addr)))
	       == NULL || prev->rl_state == CONFC_OPEN);
	/* mark items idle except the failed one */
	m0_tl_for(rcnf_active, &rconfc->rc_active, next) {
		M0_PRE(next->rl_state != CONFC_DEAD);
		next->rl_state = m0_streq(confd_addr, next->rl_confd_addr) ?
			CONFC_FAILED : CONFC_IDLE;
	} m0_tl_endfor;
	/* start from the last connected item, or from list tail otherwise */
	prev = m0_tl_find(rcnf_active, item, &rconfc->rc_active,
			  m0_streq(confd_addr, item->rl_confd_addr)) ?:
		rcnf_active_tlist_tail(&rconfc->rc_active);
	while (1) {
		/*
		 * loop through the list until successful connect or no more
		 * idle items to try
		 */
		next = rcnf_active_tlist_next(&rconfc->rc_active, prev) ?:
			rcnf_active_tlist_head(&rconfc->rc_active);
		if (next->rl_state == CONFC_FAILED)
			return M0_ERR(-ENOENT);
		rc = rconfc_conductor_connect(rconfc, next);
		if (rc == 0 && !M0_FI_ENABLED("conductor_conn_fail")) {
			next->rl_state = CONFC_OPEN;
			return M0_RC(rc);
		}
		M0_LOG(M0_ERROR, "Failed to connect to confd_addr = %s rc = %d",
		       next->rl_confd_addr, rc);
		next->rl_state = CONFC_FAILED;
		prev = next;
	}
}

static void rconfc_read_lock_get(struct m0_rconfc *rconfc)
{
	struct rlock_ctx      *rlx;
	struct m0_rm_incoming *req;

	M0_ENTRY("rconfc = %p", rconfc);
	rlx = rconfc->rc_rlock_ctx;
	req = &rlx->rlc_req;
	m0_rm_rwlock_req_init(req, &rlx->rlc_owner, &m0_rconfc_ri_ops,
			      RIF_MAY_BORROW | RIF_MAY_REVOKE, RM_RWLOCK_READ);
	m0_rm_credit_get(req);
}

static void rconfc_start_ast_cb(struct m0_sm_group *grp M0_UNUSED,
			       struct m0_sm_ast   *ast)
{
	struct m0_rconfc *rconfc = ast->sa_datum;
	struct rlock_ctx *rlx = rconfc->rc_rlock_ctx;
	int               rc;

	M0_ENTRY();
	rc = rlock_ctx_connect(rlx);
	if (rc != 0) {
		rconfc_fail(rconfc, rc);
	} else {
		rlock_ctx_owner_setup(rconfc->rc_rlock_ctx);
		rconfc_state_set(rconfc, RCS_GET_RLOCK);
		rconfc_read_lock_get(rconfc);
	}
	M0_LEAVE();
}

static void rconfc_conductor_drained(struct m0_rconfc *rconfc)
{
	/* disconnect confc until read lock being granted */
	m0_confc_reconnect(&rconfc->rc_confc, NULL, NULL);
	/* return read lock back to RM */
	rconfc_read_lock_put(rconfc);
	/* prepare for version election */
	rconfc_active_all_unlink(rconfc);
	rconfc_herd_fini(rconfc);
	rconfc->rc_ver = M0_CONF_VER_UNKNOWN;
	/* let consumer know the cache has been drained */
	if (rconfc->rc_drained_cb != NULL)
		rconfc->rc_drained_cb(rconfc);
	rconfc_state_set(rconfc, RCS_GET_RLOCK);
	rconfc_read_lock_get(rconfc);
}

/**
 * Drain confc cache because of read lock conflict.
 *
 * Waits for cache objects being entirely unpinned. This is done by just waiting
 * on a first pinned object met in cache. When the one appears unpinned, the
 * checking for other objects is repeated until no more pinned object
 * remains. Finally makes the cache empty.
 */
static void rconfc_conductor_drain(struct m0_sm_group *grp,
				   struct m0_sm_ast   *ast)
{
	struct m0_rconfc     *rconfc = ast->sa_datum;
	struct m0_conf_cache *cache = &rconfc->rc_confc.cc_cache;
	struct m0_conf_obj   *obj;
	int                   rc = 1;

	M0_ENTRY();
	m0_conf_cache_lock(cache);
	if ((obj = m0_conf_cache_pinned(cache)) != NULL) {
		m0_clink_add(&obj->co_chan, &rconfc->rc_unpinned_cl);
	} else {
		rc = _confc_cache_clean(&rconfc->rc_confc);
		if (rc != 0)
			rconfc_fail(rconfc, rc);
	}
	m0_conf_cache_unlock(cache);

	if (rc == 0)
		rconfc_conductor_drained(rconfc);
}

static bool rconfc_unpinned_cb(struct m0_clink *link)
{
	struct m0_rconfc *rconfc = container_of(
				link, struct m0_rconfc, rc_unpinned_cl);

	M0_ENTRY();
	M0_ASSERT(rconfc->rc_sm.sm_state == RCS_CONDUCTOR_DRAIN);
	m0_clink_del(link);
	rconfc_ast_post(rconfc, rconfc_conductor_drain);
	M0_LEAVE();
	return false;
}

/**
 * Called during m0_confc_ctx_init(). Confc context initialisation appears
 * blocked until rconfc allows read operations.
 *
 * @note Caller is blocked until rconfc reelection is finished. In order to
 * ensure rconfc reelection progress, caller should be in other sm group than
 * rconfc->rc_sm.sm_grp. It is responsibility of user to provide such a group
 * in m0_rconfc_init().
 *
 * @see m0_confc_gate_ops::go_check
 */
static bool rconfc_gate_check(struct m0_confc *confc)
{
	struct m0_rconfc *rconfc;

	M0_ENTRY("confc = %p", confc);
	M0_PRE(confc != NULL);
	M0_PRE(m0_mutex_is_locked(&confc->cc_lock));

	rconfc = container_of(confc, struct m0_rconfc, rc_confc);
	if (!rconfc_reading_is_allowed(rconfc)) {
		m0_mutex_unlock(&confc->cc_lock);
		m0_rconfc_lock(rconfc);
		m0_sm_timedwait(&rconfc->rc_sm,
				M0_BITS(RCS_IDLE, RCS_FAILURE),
				M0_TIME_NEVER);
		m0_rconfc_unlock(rconfc);
		m0_mutex_lock(&confc->cc_lock);
	}
	return M0_RC(rconfc_reading_is_allowed(rconfc));
}

/**
 * Called from configuration reading state machine being in S_SKIP_CONFD state.
 *
 * @see m0_confc_gate_ops::go_skip
 */
static int rconfc_gate_skip(struct m0_confc *confc)
{
	struct m0_rconfc *rconfc;

	M0_ENTRY("confc = %p", confc);
	M0_PRE(confc != NULL);
	rconfc = container_of(confc, struct m0_rconfc, rc_confc);
	return M0_RC(rconfc_conductor_iterate(rconfc));
}

/**
 * Called when all confc contexts are detached from conductor confc,
 * and therefore its cache can be cleaned.
 *
 * @see m0_confc_gate_ops::go_drain
 */
static bool rconfc_gate_drain(struct m0_clink *clink)
{
	struct m0_rconfc *rconfc;
	struct m0_confc  *confc;

	M0_ENTRY("clink = %p", clink);
	confc = container_of(clink, struct m0_confc, cc_drain);
	rconfc = container_of(confc, struct m0_rconfc, rc_confc);
	rconfc->rc_gops.go_drain = NULL;
	m0_confc_gate_ops_set(&rconfc->rc_confc, &rconfc->rc_gops);
	/*
	 * Conductor confc has no active confc contexts at this moment,
	 * so it's safe to call rconfc_ast_drain() in this sm group.
	 *
	 * If several rconfc operate in one sm group, then processing
	 * 'foreign' confc contexts is stopped for some time, but let's live
	 * with that for now, because no dead-locks are possible.
	 */
	rconfc_state_set(rconfc, RCS_CONDUCTOR_DRAIN);
	rconfc_ast_post(rconfc, rconfc_conductor_drain);
	return M0_RC(false);
}

/**
 * Allocates internally used contexts for quorum and read lock. Allocates a herd
 * of confc instances in accordance with the number of addresses of confd
 * servers to be in touch with.
 */
static int rconfc_allocate(struct m0_rconfc *rconfc,
			   const char      **confd_addr)
{
	int               rc;
	uint32_t          count = rconfc_confd_count(confd_addr);
	struct ver_accm  *va;
	struct rlock_ctx *rlx;

	M0_ENTRY();
	M0_SET0(&rconfc->rc_confc);
	rc = ver_accm_alloc_init(&va, count);
	if (rc == 0)
		rconfc->rc_qctx = va;
	else
		return M0_ERR(rc);
	M0_ALLOC_PTR(rlx);
	if (rlx == NULL)
		return M0_ERR(-ENOMEM);
	rconfc->rc_rlock_ctx = rlx;

	for (; *confd_addr != NULL; confd_addr++) {
		struct rconfc_link *lnk;
		struct m0_confc    *confc;

		M0_ALLOC_PTR(confc);
		M0_ALLOC_PTR(lnk);
		if (confc == NULL || lnk == NULL) {
			m0_free(confc);
			m0_free(lnk);
			return M0_ERR(-ENOMEM);
		}
		/* add the allocated element to herd */
		lnk->rl_rconfc     = rconfc;
		lnk->rl_confc      = confc;
		lnk->rl_confd_addr = m0_strdup(*confd_addr);
		lnk->rl_state      = CONFC_DEAD;
		rcnf_herd_tlink_init_at_tail(lnk, &rconfc->rc_herd);
	}
	return M0_RC(rc);
}

static void rlock_conflict_handle(struct m0_sm_group *grp,
				  struct m0_sm_ast   *ast)
{
	struct m0_rconfc *rconfc = ast->sa_datum;

	/* prepare for emptying conductor's cache */
	if (rconfc->rc_exp_cb != NULL)
		rconfc->rc_exp_cb(rconfc);

	/*
	 * if no context attached, call it directly, otherwise it is
	 * going to be called during the very last context finalisation
	 */
	m0_mutex_lock(&rconfc->rc_confc.cc_lock);
	if (rconfc->rc_confc.cc_nr_ctx == 0) {
		rconfc_state_set(rconfc, RCS_CONDUCTOR_DRAIN);
		rconfc_ast_post(rconfc, rconfc_conductor_drain);
	} else {
		rconfc->rc_gops.go_drain = m0_rconfc_gate_ops.go_drain;
		m0_confc_gate_ops_set(&rconfc->rc_confc,
				      &rconfc->rc_gops);
	}
	m0_mutex_unlock(&rconfc->rc_confc.cc_lock);
}

static void rconfc_rlock_windup(struct m0_rconfc *rconfc)
{
	/**
	 * Release sm group lock to prevent dead-lock with
	 * rconfc_read_lock_conflict. In worst case rlock_windup_st_in acquires
	 * locks in "rconfc sm group lock"->"rm owner lock" order and
	 * rconfc_read_lock_conflict in reverse order.
	 */
	m0_rconfc_unlock(rconfc);
	rconfc_read_lock_put(rconfc);
	m0_rconfc_lock(rconfc);
	rlock_ctx_owner_windup(rconfc->rc_rlock_ctx);
}

static void rconfc_stop_internal(struct m0_rconfc *rconfc)
{
	struct rlock_ctx *rlx = rconfc->rc_rlock_ctx;

	rconfc_herd_fini(rconfc);
	rconfc_rlock_windup(rconfc);
	if (rlx->rlc_online)
		rlock_ctx_disconnect(rlx);
	rconfc_state_set(rconfc, RCS_FINAL);
}

static void rconfc_stop_ast_cb(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_rconfc *rconfc = ast->sa_datum;

	if (M0_IN(rconfc->rc_sm.sm_state, (RCS_IDLE, RCS_FAILURE))) {
		rconfc_state_set(rconfc, RCS_STOPPING);
		rconfc_stop_internal(rconfc);
	}
	else {
		rconfc->rc_stopping = true;
	}
}

/**
 * Called when a conflicting lock acquisition is initiated somewhere in the
 * cluster. All conflicting read locks get revoked as the result firing
 * m0_rm_incoming_ops::rio_conflict() events for resource borrowers. Deprived of
 * read lock, rconfc disallows any configuration reading. As well, the cached
 * data appears outdated, and has to be dropped.
 */
static void rconfc_read_lock_conflict(struct m0_rm_incoming *in)
{
	struct m0_rconfc *rconfc;

	M0_ENTRY("in = %p", in);
	rconfc = rlock_ctx_incoming_to_rconfc(in);
	m0_rconfc_lock(rconfc);
	if (rconfc->rc_sm.sm_state == RCS_IDLE) {
		rconfc_state_set(rconfc, RCS_RLOCK_CONFLICT);
		rconfc_ast_post(rconfc, rlock_conflict_handle);
	} else {
		rconfc->rc_rlock_conflict = true;
	}
	m0_rconfc_unlock(rconfc);
	M0_LEAVE();
}

static void rconfc_idle(struct m0_rconfc *rconfc)
{
	rconfc_state_set(rconfc, RCS_IDLE);
	if (rconfc->rc_stopping) {
		rconfc_state_set(rconfc, RCS_STOPPING);
		rconfc_stop_internal(rconfc);
	} else if (rconfc->rc_rlock_conflict) {
		rconfc->rc_rlock_conflict = false;
		rconfc_state_set(rconfc, RCS_RLOCK_CONFLICT);
		rconfc_ast_post(rconfc, rlock_conflict_handle);
	}
	/*
	 * If both rc_stopping and rc_rlock_conflict flags aren't set, then
	 * SM will be idle until read lock conflict is observed provoking
	 * reelection or user requests stopping rconfc.
	 */
}

static bool rconfc_quorum_is_possible(struct m0_rconfc *rconfc)
{
	struct ver_accm    *va = rconfc->rc_qctx;
	int                 ver_count_max;
	int                 armed_count;

	armed_count = m0_tl_reduce(rcnf_herd, lnk, &rconfc->rc_herd, 0,
				   + (lnk->rl_state == CONFC_ARMED));
	ver_count_max = m0_fold(idx, acc, va->va_count, 0,
				max_type(int, acc, va->va_items[idx].vi_count));
	if (ver_count_max + armed_count < rconfc->rc_quorum) {
		M0_LOG(M0_WARN, "No chance left to reach the quorum");
		rconfc->rc_ver = M0_CONF_VER_UNKNOWN;
		/* Notify consumer about conf expired */
		if (rconfc->rc_exp_cb != NULL)
			rconfc->rc_exp_cb(rconfc);
		return false;
	}
	return true;
}

/**
 * Function tests if quorum reached to the moment.
 */
static bool rconfc_quorum_test(struct m0_rconfc *rconfc,
			       struct m0_confc *confc)
{
	struct ver_accm    *va = rconfc->rc_qctx;
	struct ver_item    *vi = NULL;
	uint64_t            ver;
	int                 idx;
	bool                quorum_reached = false;

	M0_ENTRY("rconfc = %p, confc = %p", rconfc, confc);
	M0_PRE(va != NULL);
	ver = _confc_ver_read(confc);
	M0_ASSERT(ver != M0_CONF_VER_UNKNOWN);
	for (idx = 0; idx < va->va_count; idx++) {
		if (va->va_items[idx].vi_ver == ver) {
			vi = va->va_items + idx;
			break;
		}
	}
	if (vi == NULL) {
		/* new version appeared */
		M0_ASSERT(va->va_count < va->va_total);
		vi = va->va_items + va->va_count;
		M0_PRE(vi->vi_ver == 0);
		M0_PRE(vi->vi_count == 0);
		vi->vi_ver = ver;
		++va->va_count;
	}
	++vi->vi_count;

	/* Walk along the herd and see if quorum reached. */
	for (idx = 0; idx < va->va_count; idx++) {
		if (va->va_items[idx].vi_count >= rconfc->rc_quorum) {
			/* remember the winner */
			rconfc->rc_ver = va->va_items[idx].vi_ver;
			quorum_reached = true;
			break;
		}
	}
	M0_LEAVE("result = %d", quorum_reached);
	return quorum_reached;
}

/**
 * Puts "conductor" confc in effect. In case the confc is not initialised yet,
 * the initialisation happens first inside rconfc_conductor_connect(). Then
 * connection to the very first responsive confd from active list is
 * established. Ultimately, consumer is notified about configuration expiration
 * by calling callback provided during m0_rconfc_init().
 */
static int rconfc_conductor_engage(struct m0_rconfc *rconfc)
{
	int rc = 0;

	M0_ENTRY("rconfc = %p", rconfc);
	M0_PRE(rconfc != NULL);
	M0_PRE(rconfc->rc_ver != M0_CONF_VER_UNKNOWN);
	/*
	 * See if the confc not initialized yet, or having different
	 * version compared to the newly elected one
	 */
	if (!_confc_is_inited(&rconfc->rc_confc) ||
	    _confc_ver_read(&rconfc->rc_confc) != rconfc->rc_ver) {
		/* need to connect conductor to a new version confd */
		rc = rconfc_conductor_iterate(rconfc);
	}
	return M0_RC(rc);
}

static void rconfc_version_elected(struct m0_sm_group *grp,
				   struct m0_sm_ast   *ast)
{
	struct m0_rconfc *rconfc = ast->sa_datum;
	int               rc;

	M0_ENTRY();
	rc = rconfc_quorum_is_reached(rconfc) ?
		rconfc_conductor_engage(rconfc) : -EPROTO;
	if (rc != 0)
		rconfc_fail(rconfc, rc);
	else
		rconfc_idle(rconfc);
	M0_LEAVE();
}

/**
 * Callback attached to confc context clink. Fires when reading from one of
 * confd instances is done, and therefore, the confd version is known to the
 * moment of context completion. When context is complete, the entire herd is
 * tested for quorum.
 *
 * @note Even after initialisation completion late confc replies yet still
 * possible and to be done in background filling in the active list.
 *
 * @note Callback is executed in context of rconfc sm group, because
 * corresponding confc was inited with rconfc sm group. And this group
 * is used for all confc contexts attached to confc later.
 */
static bool rconfc__cb_quorum_test(struct m0_clink *clink)
{
	struct rconfc_link *lnk;
	struct m0_rconfc   *rconfc;
	bool                quorum_before   = false;
	bool                quorum_now      = false;

	M0_ENTRY("clink = %p", clink);
	M0_PRE(clink != NULL);
	lnk = container_of(clink, struct rconfc_link, rl_clink);
	M0_ASSERT(lnk->rl_state == CONFC_ARMED);
	rconfc = lnk->rl_rconfc;
	M0_ASSERT(rconfc_is_locked(rconfc));

	if (m0_confc_ctx_is_completed(&lnk->rl_cctx)) {
		lnk->rl_rc = m0_confc_ctx_error(&lnk->rl_cctx);
		if (M0_FI_ENABLED("read_ver_failed")) {
			lnk->rl_confc->cc_cache.ca_ver = M0_CONF_VER_UNKNOWN;
			lnk->rl_rc = -ENODATA;
		}
		lnk->rl_state = lnk->rl_rc == 0 ? CONFC_OPEN : CONFC_FAILED;

		if (lnk->rl_rc == 0) {
			/*
			 * The code may be called after quorum was already
			 * reached, so we need to see if it was
			 */
			quorum_before = rconfc_quorum_is_reached(rconfc);

			quorum_now = quorum_before ||
				     rconfc_quorum_test(rconfc, lnk->rl_confc);
			if (quorum_now)
				/* Maybe add replied confc to active list */
				rconfc_active_populate(rconfc);
		}

		if ((!quorum_before && quorum_now) ||
		     !rconfc_quorum_is_possible(rconfc)) {
			rconfc_ast_post(rconfc, rconfc_version_elected);
		}
	}
	M0_LEAVE();
	return true;
}

/**
 * Version election start. Iterates through confc herd and makes every entry to
 * start asynchronous reading from corresponding confd.
 */
static void rconfc_version_elect(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_rconfc   *rconfc = ast->sa_datum;
	struct ver_accm    *va;
	struct rconfc_link *lnk;

	M0_ENTRY("rconfc = %p", rconfc);
	M0_PRE(rconfc != NULL && rconfc->rc_qctx != NULL);

	rconfc_state_set(rconfc, RCS_VERSION_ELECT);
	va = rconfc->rc_qctx;
	M0_PRE(va->va_total != 0);
	va->va_count = 0;
	m0_forall(idx, va->va_total, M0_SET0(&va->va_items[idx]));
	rconfc_herd_init(rconfc);
	/* query confd instances */
	m0_tl_for(rcnf_herd, &rconfc->rc_herd, lnk) {
		if (lnk->rl_rc == 0) {
			m0_confc_ctx_init(&lnk->rl_cctx, lnk->rl_confc);

			m0_clink_init(&lnk->rl_clink, rconfc__cb_quorum_test);
			m0_clink_add(&lnk->rl_cctx.fc_mach.sm_chan,
				     &lnk->rl_clink);
			lnk->rl_state = CONFC_ARMED;
			m0_confc_open(&lnk->rl_cctx, NULL,
					      M0_CONF_ROOT_PROFILES_FID);
		}
	} m0_tl_endfor;
}

/**
 * Called when read lock request completes.
 *
 * @param in -- read request object
 * @param rc -- read request result code indicating success (rc == 0) or failure
 */
static void rconfc_read_lock_complete(struct m0_rm_incoming *in, int32_t rc)
{
	struct m0_rconfc *rconfc;

	M0_ENTRY("in = %p, rc = %d", in, rc);
	if (rc != 0)
		M0_LOG(M0_ERROR, "Read lock request failed with rc = %d", rc);

	if (M0_FI_ENABLED("rlock_req_failed"))
		rc = M0_ERR(-ESRCH);
	rconfc = rlock_ctx_incoming_to_rconfc(in);
	M0_ASSERT(rconfc->rc_sm.sm_state == RCS_GET_RLOCK);
	if (rc == 0)
		rconfc_ast_post(rconfc, rconfc_version_elect);
	else
		rconfc_fail_ast(rconfc, rc);
	M0_LEAVE();
}

/**************************************
 * Rconfc public interface
 **************************************/

M0_INTERNAL void m0_rconfc_lock(struct m0_rconfc *rconfc)
{
	m0_sm_group_lock(rconfc->rc_sm.sm_grp);
}

M0_INTERNAL void m0_rconfc_unlock(struct m0_rconfc *rconfc)
{
	m0_sm_group_unlock(rconfc->rc_sm.sm_grp);
}

M0_INTERNAL int m0_rconfc_init(struct m0_rconfc      *rconfc,
			       const char           **confd_addr,
			       const char            *rm_addr,
			       struct m0_sm_group    *sm_group,
			       struct m0_rpc_machine *rmach,
			       uint32_t               quorum,
			       m0_rconfc_exp_cb_t     exp_cb)
{
	int      rc;
	uint32_t q;
	uint32_t count = rconfc_confd_count(confd_addr);

	M0_ENTRY("rconfc = %p, confd_addr[] = %p, [confd_addr] = %u, "
		 "quorum = %u", rconfc, confd_addr, count, quorum);
	M0_PRE(rconfc != NULL);
	M0_PRE(rmach != NULL);
	M0_PRE(confd_addr != NULL);
	M0_PRE(sm_group != NULL);
	M0_PRE(rconfc_confd_are_all_unique(confd_addr));

	M0_SET0(rconfc);
	/* validate against minimal (default) quorum value */
	q = rconfc_quorum_default_calc(count);
	M0_ASSERT(quorum == 0 || quorum >= q);

	rconfc->rc_quorum  = (quorum != 0 && quorum > q) ? quorum : q;
	rconfc->rc_rmach   = rmach;
	rconfc->rc_qctx    = NULL;
	rconfc->rc_exp_cb  = exp_cb;
	rconfc->rc_ver     = M0_CONF_VER_UNKNOWN;
	rconfc->rc_gops = (struct m0_confc_gate_ops) {
		.go_check = m0_rconfc_gate_ops.go_check,
		.go_skip  = m0_rconfc_gate_ops.go_skip,
		.go_drain = NULL,
	};
	rconfc->rc_drained_cb = NULL;

	rcnf_herd_tlist_init(&rconfc->rc_herd);
	rcnf_active_tlist_init(&rconfc->rc_active);
	m0_clink_init(&rconfc->rc_unpinned_cl, rconfc_unpinned_cb);
	m0_sm_init(&rconfc->rc_sm, &rconfc_sm_conf, RCS_INIT, sm_group);

	rc = rconfc_allocate(rconfc, confd_addr) ?:
	     rlock_ctx_init(rconfc->rc_rlock_ctx, rconfc,
			    rconfc->rc_rmach, rm_addr);
	if (rc != 0) {
		m0_rconfc_fini(rconfc);
		return M0_ERR(rc);
	}
	return M0_RC(rc);
}

M0_INTERNAL void m0_rconfc_start(struct m0_rconfc *rconfc)
{
	M0_ENTRY("rconfc %p", rconfc);
	m0_rconfc_lock(rconfc);
	rconfc_state_set(rconfc, RCS_STARTING);
	rconfc_ast_post(rconfc, rconfc_start_ast_cb);
	m0_rconfc_unlock(rconfc);
	M0_LEAVE();
}

M0_INTERNAL int m0_rconfc_start_sync(struct m0_rconfc *rconfc)
{
	struct m0_clink clink;

	m0_clink_init(&clink, NULL);
	clink.cl_is_oneshot = true;
	m0_rconfc_start(rconfc);
	m0_rconfc_lock(rconfc);
	m0_sm_timedwait(&rconfc->rc_sm,
			M0_BITS(RCS_IDLE, RCS_FAILURE),
			M0_TIME_NEVER);
	m0_rconfc_unlock(rconfc);
	m0_clink_fini(&clink);
	return M0_RC(rconfc->rc_sm.sm_rc);
}

M0_INTERNAL void m0_rconfc_stop(struct m0_rconfc *rconfc)
{
	M0_ENTRY("rconfc %p", rconfc);
	rconfc->rc_stop_ast.sa_cb    = rconfc_stop_ast_cb;
	rconfc->rc_stop_ast.sa_datum = rconfc;
	m0_sm_ast_post(rconfc->rc_sm.sm_grp, &rconfc->rc_stop_ast);
	M0_LEAVE();
}

M0_INTERNAL void m0_rconfc_stop_sync(struct m0_rconfc *rconfc)
{
	M0_ENTRY();
	m0_rconfc_stop(rconfc);
	m0_rconfc_lock(rconfc);
	m0_sm_timedwait(&rconfc->rc_sm, M0_BITS(RCS_FINAL), M0_TIME_NEVER);
	m0_rconfc_unlock(rconfc);
	M0_LEAVE();
}

M0_INTERNAL void m0_rconfc_fini(struct m0_rconfc *rconfc)
{
	M0_ENTRY("rconfc = %p", rconfc);
	M0_PRE(rconfc != NULL);

	m0_free(rconfc->rc_qctx);
	if (rlock_ctx_is_initialised(rconfc->rc_rlock_ctx))
		rlock_ctx_fini(rconfc->rc_rlock_ctx);
	m0_free(rconfc->rc_rlock_ctx);
	if (_confc_is_inited(&rconfc->rc_confc))
		m0_confc_fini(&rconfc->rc_confc);
	rconfc_active_all_unlink(rconfc);
	rcnf_active_tlist_fini(&rconfc->rc_active);
	rconfc_herd_prune(rconfc);
	rcnf_herd_tlist_fini(&rconfc->rc_herd);
	m0_rconfc_lock(rconfc);
	m0_sm_fini(&rconfc->rc_sm);
	m0_rconfc_unlock(rconfc);

	M0_LEAVE();
}

M0_INTERNAL uint64_t m0_rconfc_ver_max_read(struct m0_rconfc *rconfc)
{
	uint64_t ver_max;

	M0_ENTRY("rconfc = %p", rconfc);
	m0_rconfc_lock(rconfc);
	ver_max = m0_tl_fold(rcnf_herd, lnk, acc, &rconfc->rc_herd,
			     M0_CONF_VER_UNKNOWN,
			     max64(acc, _confc_ver_read(lnk->rl_confc)));
	m0_rconfc_unlock(rconfc);
	M0_LEAVE("ver_max = %"PRIu64, ver_max);
	return ver_max;
}

M0_INTERNAL void m0_rconfc_exp_cb_set(struct m0_rconfc   *rconfc,
				      m0_rconfc_exp_cb_t  cb)
{
	M0_PRE(rconfc_is_locked(rconfc));
	rconfc->rc_exp_cb = cb;
}

M0_INTERNAL void m0_rconfc_drained_cb_set(struct m0_rconfc       *rconfc,
					  m0_rconfc_drained_cb_t  cb)
{
	M0_PRE(rconfc_is_locked(rconfc));
	rconfc->rc_drained_cb = cb;
}

/** @} rconfc_dlspec */
#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
