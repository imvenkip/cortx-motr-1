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
#include "rm/rm_rwlock.h"  /* m0_rm_rw_lock */
#include "rm/rm_service.h" /* m0_rm_svc_rwlock_get */
#include "rm/rm_rwlock.h"
#include "rpc/rpclib.h"    /* m0_rpc_client_connect */
#include "conf/cache.h"
#include "conf/confc.h"
#include "conf/rconfc.h"
#include "conf/rconfc_internal.h"

/**
 * @page rconfc-lspec rconfc Internals
 *
 * - @ref rconfc-lspec-elect
 * - @ref rconfc-lspec-rlock
 * - @ref rconfc-lspec-gate
 *   - @ref rconfc-lspec-gate-check
 *   - @ref rconfc-lspec-gate-drain
 *   - @ref rconfc-lspec-gate-skip
 * - @ref rconfc_dlspec "Detailed Logical Specification"
 *
 * <hr> <!------------------------------------------------------------>
 * @section rconfc-lspec-rlock Read Lock Acquisition and Revocation
 *
 * During m0_rconfc_init() execution after rconfc internal structures are
 * successfully allocated, m0_rconfc_init() continues execution requesting read
 * lock from Resource Manager (RM) by calling rconfc_read_lock_wait(). On
 * request completion rconfc_read_lock_complete() is called. Successful lock
 * acquisition indicates no configuration change is in progress and
 * configuration reading is allowed.
 *
 * The read lock is retained by rconfc instance until finalisation. But the lock
 * can be revoked by RM in case a conflicting lock is requested. On the lock
 * revocation rconfc_read_lock_conflict() is called. The call installs
 * m0_confc_gate_ops::go_drain() callback to be notified when the last reading
 * context is detached from m0_rconfc::rc_confc instance. The callback ends in
 * calling rconfc_gate_drain() where m0_rconfc::rc_drain_ast is queued. The AST
 * executes rconfc_ast_drain(), where rconfc eventually puts the read lock back
 * to RM.
 *
 * Being informed about the conflict, rconfc disallows configuration reading
 * done via m0_rconfc::rc_confc until the next read lock acquisition. Besides,
 * in rconfc_ast_drain() the mentioned confc's cache is drained to prevent
 * consumer from reading cached-but-outdated configuration values. However, the
 * cache data remains untouched and readable to the very moment when there is no
 * cache object pinned anymore, and the last reading context detaches from the
 * confc being in use.
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
 * read lock acquisition rconfc_version_elect() is called. The call initialises
 * every confc instance of the m0_rconfc::rc_herd list, attaches
 * rconfc__cb_quorum_test() to its context and initiates asynchronous reading
 * from the corresponding confd server. Then rconfc_version_elect() is put in
 * waiting on m0_rconfc::rc_ver_done semaphore until version quorum is either
 * reached or found impossible.
 *
 * On every reading event rconfc__cb_quorum_test() is called. In case the
 * reading context is not completed, the function returns zero value indicating
 * the process to go on. Otherwise rconfc_quorum_test() is called to see if
 * quorum is reached with the last reply. If quorum is reached or impossible,
 * the m0_rconfc::rc_ver_done semaphore is signaled letting
 * rconfc_version_elect() wake up and return.
 *
 * Quorum is considered reached when the number of confd servers reported the
 * same version number is greater or equal to the value provided to
 * m0_rconfc_init(). In case zero value was provided, the required quorum number
 * is automatically calculated as a half of confd server count plus one.
 *
 * When quorum reached, rconfc_conductor_engage() is called connecting
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
   wordwraparcs="1", hscale="1.5";
   rm[ label = RM ],
   rc[ label = rconfc, linecolor="#0000ff", textcolor="#0000ff" ],
   c [ label = "rconfc->rc_confc", linecolor="#0000ff", textcolor="#0000ff" ],
   x [ label = confc_ctx ],
   m [ label = consumer ];

   ||| ;
   rm note rm [ label = "grant read lock", textcolor="#00aa00" ];
   rm=>>rc [ label = "rconfc_read_lock_complete(rc = 0)", textcolor="#00aa00"];
   rc=>rc  [ label = "rconfc_version_elect" ];
   rc=>rc  [ label = "rconfc_conductor_engage" ];
   rc note rc [ label = "reading allowed = true" ];
   ... ;
   m=>x   [ label = "m0_confc_ctx_init"];
   x=>c   [ label = "get .go_check"];
   x<<c   [ label = ".go_check"];
   ---    [ label = "if (.go_check != NULL)" ];
   x=>>rc [ label = "rconfc_gate_check"];
   rc>>x  [ label = "allowed = true" ];
   ---    [ label = "else // reading allowed by default"];
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
   wordwraparcs="1", hscale="1.5";
   rm[ label = RM ],
   at[ label = AST_thread ],
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
   at<<=rc [ label = "post rconfc_ast_drain" ];
   at=>at  [ label = "_confc_cache_drain" ];
   at=>c   [ label = "m0_confc_reconnect(NULL)" ];
   at=>>rm [ label = "rconfc_read_lock_put" ];
   at=>>rm [ label = "rconfc_read_lock_get" ];
   ... ;
   m=>x   [ label = "m0_confc_ctx_init"];
   x=>c   [ label = "get .go_check"];
   x<<c   [ label = ".go_check"];
   ---    [ label = "if (.go_check != NULL)" ];
   x=>>rc [ label = "rconfc_gate_check"];
   rc=>rc [ label = "add m0_confc::cc_check clink to m0_rconfc::rc_gate chan" ];
   rc=>rc [ label = "wait on m0_rconfc::rc_gate channel" ];
   ---    [ label = "waiting in rconfc_gate_check", linecolor="#0000ff",
   textcolor="#0000ff"];
   ... ;
   rm note rm [ label = "grant read lock", textcolor="#00aa00" ];
   rm=>>rc [ label = "rconfc_read_lock_complete(rc = 0)", textcolor="#00aa00"];
   rc=>rc  [ label = "rconfc_version_elect" ];
   rc=>rc  [ label = "rconfc_conductor_engage" ];
   rc note rc [ label = "reading allowed = true" ];
   rc=>rc [ label = "broadcast on m0_rconfc::rc_gate" ];
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
   wordwraparcs="1", hscale="1.5";
   rm[ label = RM ],
   at[ label = AST_thread ],
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
   at<<=rc [ label = "post rconfc_ast_drain" ];
   at=>at  [ label = "_confc_cache_drain" ];
   at=>c   [ label = "m0_confc_reconnect(NULL)" ];
   at=>>rm [ label = "rconfc_read_lock_put" ];
   at=>>rm [ label = "rconfc_read_lock_get" ];
   ... ;
   m=>x   [ label = "m0_confc_ctx_init"];
   x=>c   [ label = "get .go_check"];
   x<<c   [ label = ".go_check"];
   ---    [ label = "if (.go_check != NULL)" ];
   x=>>rc [ label = "rconfc_gate_check"];
   rc=>rc [ label = "add m0_confc::cc_check clink to m0_rconfc::rc_gate chan" ];
   rc=>rc [ label = "wait on m0_rconfc::rc_gate channel" ];
   ---    [ label = "waiting in rconfc_gate_check", linecolor="#0000ff",
   textcolor="#0000ff"];
   ... ;
   rm note rc [ label = "communication failed", textcolor="#cc0000" ];
   rm=>>rc [ label = "rconfc_read_lock_complete(rc != 0)", textcolor="#cc0000"];
   rc note rc [ label = "reading allowed = false" ];
   rc=>rc [ label = "broadcast on m0_rconfc::rc_gate" ];
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
 * @b Diag.3: @b "Reading remains disallowed because of RM communication failure"
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
 * invoked by posting m0_rconfc::rc_drain_ast. The AST internally calls
 * _confc_cache_drop() where cache is waited for having all objects unpinned,
 * and only then it gets emptied.
 *

   @msc
   wordwraparcs="1", hscale="1.5";
   rm[ label = RM ],
   at[ label = AST_thread ],
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
   rc=>>rc [ label = "rconfc_gate_drain" ];
   at<-rc  [ label = "post m0_rconfc::rc_drain_ast", linecolor="#0000ff",
   textcolor="#0000ff" ];
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
   at<-rc  [ label = "post m0_rconfc::rc_drain_ast", linecolor="#0000ff",
   textcolor="#0000ff" ];
   rc>>x   [ label = "return" ];
   at->at  [ label = "rconfc_ast_drain ==> _confc_cache_drop" ];
   ---     [ label = " loop until no pinned object remains ", linecolor="#00aaaa",
   textcolor="#00aaaa" ];
   at=>h   [ label = "find first pinned object" ];
   h>>at   [ label = "return m0_conf_obj" ];
   at=>at  [ label = "wait for object closure" ];
   ---     [ label = "waiting in _confc_cache_drop" ];
   ... ;
   m=>h    [ label = "m0_confc_close(waited conf object)" ];
   h->at   [ label = "broadcast obj->co_chan" ];
   h>>m    [ label = "return" ];
   ---     [ label = " wake up in _confc_cache_drop and loop back to finding a
   pinned object ", linecolor="#00aaaa", textcolor="#00aaaa" ];
   at=>h   [ label = "m0_conf_cache_empty" ];
   h=>h    [ label = "delete all cached objects" ];
   h>>at   [ label = "return" ];
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
	struct m0_rm_owner  *owner;
	struct m0_rm_remote *creditor;

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
	m0_rw_lockable_init(&rlx->rlc_rwlock, &M0_RWLOCK_FID,
			    m0_rwlockable_domain());
	owner            = &rlx->rlc_owner;
	creditor         = &rlx->rlc_creditor;
	m0_fid_tgenerate(&rlx->rlc_owner_fid, M0_RM_OWNER_FT);
	m0_rm_rwlock_owner_init(owner, &rlx->rlc_owner_fid,
				&rlx->rlc_rwlock, NULL);
	m0_rm_remote_init(creditor, owner->ro_resource);
	creditor->rem_session = &rlx->rlc_sess;
	creditor->rem_cookie  = M0_COOKIE_NULL;
	owner->ro_creditor    = creditor;
	m0_semaphore_init(&rlx->rlc_completion, 0);

	return M0_RC(rlock_ctx_connect(rlx));
}

static void rlock_ctx_fini(struct rlock_ctx *rlx)
{
	int rc;

	m0_rm_owner_windup(&rlx->rlc_owner);
	rc = m0_rm_owner_timedwait(&rlx->rlc_owner,
				   M0_BITS(ROS_FINAL, ROS_INSOLVENT),
				   M0_TIME_NEVER);
	M0_ASSERT(rc == 0);
	m0_rm_remote_fini(&rlx->rlc_creditor);
	m0_rm_rwlock_owner_fini(&rlx->rlc_owner);
	if (rlx->rlc_online)
		rlock_ctx_disconnect(rlx);
	m0_free0(&rlx->rlc_rm_addr);
	m0_rw_lockable_fini(&rlx->rlc_rwlock);
	m0_semaphore_fini(&rlx->rlc_completion);
	M0_SET0(rlx);
}

static void rlock_ctx_reading_set(struct rlock_ctx *rlx, bool allowed)
{
	rlx->rlc_allowed = allowed;
}

static bool rlock_ctx_reading_is_allowed(const struct rlock_ctx *rlx)
{
	return rlx->rlc_allowed;
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

/**
 * Waits for cache objects being entirely unpinned. This is done by just waiting
 * on a first pinned object met in cache. When the one appears unpinned, the
 * checking for other objects is repeated until no more pinned object
 * remains. Finally makes the cache empty.
 */
static void _confc_cache_drop(struct m0_confc *confc)
{
	struct m0_conf_obj *obj;
	struct m0_clink     clink;

	M0_ENTRY();
	m0_clink_init(&clink, NULL);
	m0_conf_cache_lock(&confc->cc_cache);
	while ((obj = m0_conf_cache_pinned(&confc->cc_cache)) != NULL) {
		m0_clink_add(&obj->co_chan, &clink);
		m0_conf_cache_unlock(&confc->cc_cache);
		m0_chan_wait(&clink);
		m0_conf_cache_lock(&confc->cc_cache);
		m0_clink_del(&clink);
	}
	m0_clink_fini(&clink);
	m0_conf_cache_clean(&confc->cc_cache);
	m0_conf_cache_unlock(&confc->cc_cache);
	M0_LEAVE();
}

static bool _confc_is_inited(struct m0_confc *confc)
{
	return confc->cc_group != NULL;
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

static void rconfc_lock(struct m0_rconfc *rconfc)
{
	m0_mutex_lock(&rconfc->rc_lock);
}

static void rconfc_unlock(struct m0_rconfc *rconfc)
{
	m0_mutex_unlock(&rconfc->rc_lock);
}

static bool rconfc_reading_is_allowed(const struct m0_rconfc *rconfc)
{
	M0_PRE(rconfc != NULL && rconfc->rc_rlock_ctx != NULL);
	return rlock_ctx_reading_is_allowed(rconfc->rc_rlock_ctx);
}

/***************************************
 * Rconfc private part
 ***************************************/

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
		lnk->rl_rc = m0_confc_init(lnk->rl_confc, rconfc->rc_sm_group,
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
			m0_clink_del_lock(&lnk->rl_clink);
			m0_clink_fini(&lnk->rl_clink);
			m0_confc_ctx_fini(&lnk->rl_cctx);
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
			rcnf_active_tlink_init_at_tail(lnk,
						       &rconfc->
						       rc_active);
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
		rc = m0_confc_init(&rconfc->rc_confc, rconfc->rc_sm_group,
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

/** Read Lock request */
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
	M0_LEAVE();
}

/**
 * Asynchronous system trap for draining confc cache because of read lock
 * conflict. When done with cache, it cancels read lock rconfc currently owns,
 * and initiates another read lock borrowing procedure. Invoked from
 * rconfc_gate_drain().
 */
static void rconfc_ast_drain(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_rconfc *rconfc;

	M0_ENTRY();
	rconfc = ast->sa_datum;
	_confc_cache_drop(&rconfc->rc_confc);
	/* disconnect confc until read lock being granted */
	m0_confc_reconnect(&rconfc->rc_confc, NULL, NULL);
	/* return read lock back to RM */
	rconfc_read_lock_put(rconfc);
	/* prepare for version election */
	rconfc_active_all_unlink(rconfc);
	rconfc_herd_fini(rconfc);
	rconfc->rc_ver = M0_CONF_VER_UNKNOWN;
	/*
	 * request for a new read lock. Rconfc gate remains locked for reading
	 * until the read lock is granted, i.e. rconfc_read_lock_complete() is
	 * called unlocking rconfc for reading
	 */
	rconfc_read_lock_get(rconfc);
	/* let consumer know the cache has been drained */
	rconfc_lock(rconfc);
	if (rconfc->rc_drained_cb != NULL)
		rconfc->rc_drained_cb(rconfc);
	rconfc_unlock(rconfc);
	M0_LEAVE();
}

/**
 * Called during m0_confc_ctx_init(). Confc context initialisation appears
 * blocked until rconfc allows read operations.
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
		m0_clink_add_lock(&rconfc->rc_gate, &confc->cc_check);
		m0_chan_wait(&confc->cc_check);
		m0_clink_del_lock(&confc->cc_check);
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
 * Called when read lock is revoked, and therefore cache cleanup is
 * required. Inside the call the asynchronous callback rconfc_ast_drain() is
 * posted in m0_rconfc::rc_drain_ast.
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
	M0_RCONFC_CB_SET_LOCK(rconfc, rc_gops.go_drain, NULL);
	m0_confc_gate_ops_set(&rconfc->rc_confc, &rconfc->rc_gops);
	rconfc->rc_drain_ast = (struct m0_sm_ast) {
		.sa_datum = rconfc,
		.sa_cb    = rconfc_ast_drain,
	};
	m0_sm_ast_post(m0_locality0_get()->lo_grp, &rconfc->rc_drain_ast);
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
		rconfc_lock(rconfc);
		rcnf_herd_tlink_init_at_tail(lnk, &rconfc->rc_herd);
		rconfc_unlock(rconfc);
	}
	return M0_RC(rc);
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
		if (_confc_is_inited(&rconfc->rc_confc))
			m0_confc_fini(&rconfc->rc_confc);
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
 * Callback attached to confc context clink. Fires when reading from one of
 * confd instances is done, and therefore, the confd version is known to the
 * moment of context completion. When context is complete, the entire herd is
 * tested for quorum.
 *
 * Note that even after initialisation completion late confc replies yet still
 * possible and to be done in background filling in the active list.
 */
static bool rconfc__cb_quorum_test(struct m0_clink *clink)
{
	struct rconfc_link *lnk;
	struct m0_rconfc   *rconfc;
	bool                res;
	bool                quorum_before   = false;
	bool                quorum_now      = false;

	M0_ENTRY("clink = %p", clink);
	M0_PRE(clink != NULL);
	lnk = container_of(clink, struct rconfc_link, rl_clink);
	M0_ASSERT(lnk->rl_state == CONFC_ARMED);
	rconfc = lnk->rl_rconfc;

	rconfc_lock(rconfc);
	res = m0_confc_ctx_is_completed(&lnk->rl_cctx);

	if (res) {
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
			quorum_before = m0_rconfc_quorum_is_reached(rconfc);

			quorum_now = quorum_before ||
				     rconfc_quorum_test(rconfc, lnk->rl_confc);
			if (quorum_now)
				/* Maybe add replied confc to active list */
				rconfc_active_populate(rconfc);
		}

		/*
		 * signal only when there was no quorum before the test
		 * began, or quorum is not possible anymore, to provide
		 * single semaphore thrust
		 */
		if ((!quorum_before && quorum_now) ||
		     !rconfc_quorum_is_possible(rconfc))
			m0_semaphore_up(&rconfc->rc_ver_done);
	}
	rconfc_unlock(rconfc);
	M0_LEAVE("return = %d", !res);
	return !res;
}

/**
 * Version election start. Iterates through confc herd and makes every entry to
 * start asynchronous reading from corresponding confd. Waits for conclusion on
 * quorum until being reached or found impossible.
 */
static int rconfc_version_elect(struct m0_rconfc *rconfc)
{
	struct ver_accm    *va;
	struct rconfc_link *lnk;
	int                 rc;

	M0_ENTRY("rconfc = %p", rconfc);
	M0_PRE(rconfc != NULL && rconfc->rc_qctx != NULL);
	va = rconfc->rc_qctx;
	M0_PRE(va->va_total != 0);
	va->va_count = 0;
	m0_forall(idx, va->va_total, M0_SET0(&va->va_items[idx]));
	rconfc_herd_init(rconfc);
	rc = m0_semaphore_init(&rconfc->rc_ver_done, 0);

	/* query confd instances */
	m0_tl_for(rcnf_herd, &rconfc->rc_herd, lnk) {
		if (lnk->rl_rc == 0) {
			m0_confc_ctx_init(&lnk->rl_cctx, lnk->rl_confc);
			m0_clink_init(&lnk->rl_clink, rconfc__cb_quorum_test);
			m0_clink_add_lock(&lnk->rl_cctx.fc_mach.sm_chan,
					  &lnk->rl_clink);
			lnk->rl_state = CONFC_ARMED;
			lnk->rl_rc =
				m0_confc_open(&lnk->rl_cctx, NULL,
					      M0_CONF_ROOT_PROFILES_FID);
			if (lnk->rl_rc != 0)
				lnk->rl_state = CONFC_FAILED;

		}
	} m0_tl_endfor;

	/* wait until quorum reached */
	m0_semaphore_down(&rconfc->rc_ver_done);
	m0_semaphore_fini(&rconfc->rc_ver_done);

	return M0_RC(rc);
}

static void rconfc_cleanup(struct m0_rconfc *rconfc)
{
	M0_ENTRY("rconfc = %p", rconfc);
	rconfc_lock(rconfc);
	m0_free(rconfc->rc_qctx);
	if (rlock_ctx_is_initialised(rconfc->rc_rlock_ctx)) {
		rconfc_read_lock_put(rconfc);
		rlock_ctx_fini(rconfc->rc_rlock_ctx);
	}
	m0_free(rconfc->rc_rlock_ctx);
	rconfc_active_all_unlink(rconfc);
	if (_confc_is_inited(&rconfc->rc_confc))
		m0_confc_fini(&rconfc->rc_confc);
	rconfc_herd_fini(rconfc);
	rconfc_herd_prune(rconfc);
	rconfc_unlock(rconfc);
	M0_LEAVE();
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
	rconfc_lock(rconfc);
	/*
	 * See if the confc not initialized yet, or having different
	 * version compared to the newly elected one
	 */
	if (!_confc_is_inited(&rconfc->rc_confc) ||
	    _confc_ver_read(&rconfc->rc_confc) != rconfc->rc_ver) {
		/* need to connect conductor to a new version confd */
		rc = rconfc_conductor_iterate(rconfc);
	}
	rconfc_unlock(rconfc);
	return M0_RC(rc);
}

/**
 * Read Lock request, blocking.
 *
 * @see rconfc_read_lock_get()
 */
static int rconfc_read_lock_wait(struct m0_rconfc *rconfc)
{
	struct rlock_ctx *rlx;

	M0_ENTRY("rconfc = %p", rconfc);
	rlx = rconfc->rc_rlock_ctx;
	rconfc_read_lock_get(rconfc);
	m0_semaphore_down(&rlx->rlc_completion);
	return M0_RC(rlx->rlc_rc);
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
	struct rlock_ctx *rlx;

	M0_ENTRY("in = %p, rc = %d", in, rc);
	if (rc != 0)
		M0_LOG(M0_ERROR, "Read lock request failed with rc = %d", rc);

	if (M0_FI_ENABLED("rlock_req_failed"))
		rc = M0_ERR(-ESRCH);
	rconfc = rlock_ctx_incoming_to_rconfc(in);
	rlx = rconfc->rc_rlock_ctx;
	rlx->rlc_rc = rc;
	/* now it's time to start version election */
	if (rc == 0)
		rlx->rlc_rc = rconfc_version_elect(rconfc) ?:
			m0_rconfc_quorum_is_reached_lock(rconfc) ?
			rconfc_conductor_engage(rconfc) : 0;
	rlock_ctx_reading_set(rlx, rc == 0);
	/**
	 * @todo There's a potential problem here. When rc != 0 the reading is
	 * disallowed, so any following m0_confc_ctx_init() is going to hang on
	 * waiting for m0_rconfc::rc_gate channel being signaled.
	 */
	/* Allow reading through conductor for all blocked contexts */
	m0_chan_broadcast_lock(&rconfc->rc_gate);
	m0_semaphore_up(&rlx->rlc_completion);
	M0_LEAVE();
}

/**
 * Called when a conflicting lock acquisition is initiated somewhere in the
 * cluster. All conflicting read locks get revoked as the result firing
 * m0_rm_incoming_ops::rio_conflict() events for resource borrowers. Deprived of
 * read lock, rconfc disallows any configuration reading. As well, the cached
 * data appears outdated, and has to be dropped. The asynchronous confc cache
 * dropping is initiated during the call, or scheduled until the very last
 * reading context detach. Ultimately, rconfc re-initiates read lock request.
 *
 * @note The request is going to be completed only after write lock is canceled
 * by spiel.
 */
static void rconfc_read_lock_conflict(struct m0_rm_incoming *in)
{
	struct m0_rconfc *rconfc;

	M0_ENTRY("in = %p", in);
	/* forbid any reading via conductor */
	rconfc = rlock_ctx_incoming_to_rconfc(in);
	rlock_ctx_reading_set(rconfc->rc_rlock_ctx, false);
	/* prepare for emptying conductor's cache */
	rconfc_lock(rconfc);
	if (rconfc->rc_exp_cb != NULL)
		rconfc->rc_exp_cb(rconfc);
	rconfc_unlock(rconfc);

	/*
	 * if no context attached, call it directly, otherwise it is going to be
	 * called during the very last context finalisation
	 */
	m0_mutex_lock(&rconfc->rc_confc.cc_lock);
	if (rconfc->rc_confc.cc_nr_ctx == 0) {
		m0_rconfc_gate_ops.go_drain(&rconfc->rc_confc.cc_drain);
	} else {
		M0_RCONFC_CB_SET_LOCK(rconfc, rc_gops.go_drain,
				      m0_rconfc_gate_ops.go_drain);
		m0_confc_gate_ops_set(&rconfc->rc_confc, &rconfc->rc_gops);
	}
	m0_mutex_unlock(&rconfc->rc_confc.cc_lock);
	M0_LEAVE();
}

/**************************************
 * Rconfc public interface
 **************************************/

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
	rconfc->rc_sm_group = sm_group;
	rconfc->rc_drained_cb = NULL;

	m0_mutex_init(&rconfc->rc_lock);
	m0_chan_init(&rconfc->rc_gate, &rconfc->rc_lock);
	rcnf_herd_tlist_init(&rconfc->rc_herd);
	rcnf_active_tlist_init(&rconfc->rc_active);

	rc = rconfc_allocate(rconfc, confd_addr);
	if (rc == 0) {
		rc = rlock_ctx_init(rconfc->rc_rlock_ctx, rconfc,
				    rconfc->rc_rmach, rm_addr) ?:
			rconfc_read_lock_wait(rconfc);
		/*
		 * Warning: do not dismantle quorum context rconfc->rc_qctx here
		 * as some confc instances still may reply after quorum reached
		 * already, and therefore, still need it to refresh active list
		 */
	}

	return M0_RC(rc);
}

M0_INTERNAL void m0_rconfc_fini(struct m0_rconfc *rconfc)
{
	M0_ENTRY("rconfc = %p", rconfc);
	M0_PRE(rconfc != NULL);

	rconfc_cleanup(rconfc);
	rcnf_active_tlist_fini(&rconfc->rc_active);
	rcnf_herd_tlist_fini(&rconfc->rc_herd);
	rconfc_lock(rconfc);
	m0_chan_fini(&rconfc->rc_gate);
	rconfc_unlock(rconfc);
	m0_mutex_fini(&rconfc->rc_lock);

	M0_LEAVE();
}

M0_INTERNAL bool m0_rconfc_quorum_is_reached(struct m0_rconfc *rconfc)
{
	M0_PRE(m0_mutex_is_locked(&rconfc->rc_lock));
	return rconfc->rc_ver != M0_CONF_VER_UNKNOWN;
}

M0_INTERNAL bool m0_rconfc_quorum_is_reached_lock(struct m0_rconfc *rconfc)
{
	bool q;

	rconfc_lock(rconfc);
	q = m0_rconfc_quorum_is_reached(rconfc);
	rconfc_unlock(rconfc);
	return q;
}

M0_INTERNAL uint64_t m0_rconfc_ver_max_read(struct m0_rconfc *rconfc)
{
	uint64_t ver_max = M0_CONF_VER_UNKNOWN;

	M0_ENTRY("rconfc = %p", rconfc);
	rconfc_lock(rconfc);
	ver_max = m0_tl_fold(rcnf_herd, lnk, acc, &rconfc->rc_herd, ver_max,
			     max64(acc, _confc_ver_read(lnk->rl_confc)));
	rconfc_unlock(rconfc);
	M0_LEAVE("ver_max = %"PRIu64, ver_max);
	return ver_max;
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
