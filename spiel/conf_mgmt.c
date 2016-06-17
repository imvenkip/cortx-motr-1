/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Mikhail Antropov <mikhail.v.antropov@seagate.com>
 * Original creation date: 11-Feb-2015
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SPIEL
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/finject.h"       /* M0_FI_ENABLED */
#include "lib/memory.h"        /* m0_alloc, M0_ALLOC_ARR */
#include "lib/string.h"        /* m0_strdup, m0_strings_dup */
#include "lib/tlist.h"
#include "fid/fid.h"
#include "conf/cache.h"
#include "conf/flip_fop.h"
#include "conf/load_fop.h"
#include "conf/obj.h"
#include "conf/obj_ops.h"
#include "conf/objs/common.h"
#include "conf/onwire_xc.h"    /* m0_confx_xc */
#include "conf/preload.h"      /* m0_confx_free, m0_confx_to_string */
#include "rm/rm_rwlock.h"      /* m0_rw_lockable */
#include "rpc/link.h"
#include "rpc/rpclib.h"        /* m0_rpc_client_connect */
#include "ioservice/fid_convert.h" /* M0_FID_DEVICE_ID_MAX */
#include "spiel/spiel.h"
#include "spiel/conf_mgmt.h"
#include "spiel/spiel_internal.h"
#ifndef __KERNEL__
#  include <stdio.h>           /* FILE, fopen */
#endif

/**
 * @addtogroup spiel-api-fspec-intr
 * @{
 */

M0_TL_DESCR_DECLARE(rpcbulk, M0_EXTERN);

struct spiel_conf_param {
	const struct m0_fid            *scp_fid;
	const struct m0_conf_obj_type  *scp_type;
	struct m0_conf_obj            **scp_obj;
};

static int spiel_rwlockable_write_domain_init(struct m0_spiel_wlock_ctx *wlx)
{
	return m0_rwlockable_domain_type_init(&wlx->wlc_dom, &wlx->wlc_rt);
}

static void spiel_rwlockable_write_domain_fini(struct m0_spiel_wlock_ctx *wlx)
{
	m0_rwlockable_domain_type_fini(&wlx->wlc_dom, &wlx->wlc_rt);
}

#define SPIEL_CONF_CHECK(cache, ...)                                     \
	spiel_conf_parameter_check(cache, (struct spiel_conf_param []) { \
						__VA_ARGS__,             \
						{ NULL, NULL, NULL }, });

static int spiel_root_add(struct m0_spiel_tx *tx)
{
	int                  rc;
	struct m0_conf_obj  *obj;
	struct m0_conf_root *root;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);
	rc = m0_conf_obj_find(&tx->spt_cache, &M0_CONF_ROOT_FID, &obj);
	if (obj != NULL && obj->co_status != M0_CS_MISSING)
		rc = M0_ERR(-EEXIST);
	if (rc == 0) {
		root = M0_CONF_CAST(obj, m0_conf_root);
		/* Real version number will be set during transaction commit
		 * one of the two ways:
		 *
		 * 1. Spiel client may provide version number explicitly when
		 * calling m0_spiel_tx_commit_forced().
		 *
		 * 2. In case client does not provide any valid version number,
		 * current maximum version number known among confd services is
		 * going to be fetched by rconfc. This value to be incremented
		 * and used as version number of currently composed conf DB.
		 */
		root->rt_verno = M0_CONF_VER_TEMP;
		rc = dir_new_adopt(&tx->spt_cache, &root->rt_obj,
				   &M0_CONF_ROOT_PROFILES_FID,
				   &M0_CONF_PROFILE_TYPE, NULL,
				   &root->rt_profiles);
		if (rc == 0)
			obj->co_status = M0_CS_READY;
	}
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);
}

/**
 * Overrides configuration version number originally set during
 * m0_spiel_tx_open() and stored in root object.
 */
static int spiel_root_ver_update(struct m0_spiel_tx *tx, uint64_t verno)
{
	int                  rc;
	struct m0_conf_obj  *obj;
	struct m0_conf_root *root;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);
	rc = m0_conf_obj_find(&tx->spt_cache, &M0_CONF_ROOT_FID, &obj);
	if (obj == NULL || obj->co_status == M0_CS_MISSING)
		rc = M0_ERR(-ENOENT);
	if (rc == 0) {
		root = M0_CONF_CAST(obj, m0_conf_root);
		root->rt_verno = verno;
	}
	m0_mutex_unlock(&tx->spt_lock);

	return M0_RC(rc);
}

void m0_spiel_tx_open(struct m0_spiel    *spiel,
		      struct m0_spiel_tx *tx)
{
	int rc;

	M0_PRE(tx != NULL);
	M0_ENTRY("tx %p", tx);

	tx->spt_spiel = spiel;
	tx->spt_buffer = NULL;
	m0_mutex_init(&tx->spt_lock);
	m0_conf_cache_init(&tx->spt_cache, &tx->spt_lock);
	rc = spiel_root_add(tx);
	M0_ASSERT(rc == 0);
	M0_LEAVE();
}
M0_EXPORTED(m0_spiel_tx_open);

/**
 *  Check cache for completeness:
 *  each element has state M0_CS_READY and
 *  has real parent (if last need by obj type)
 */
int m0_spiel_tx_validate(struct m0_spiel_tx *tx)
{
	struct m0_conf_obj   *obj;
	struct m0_conf_obj   *obj_parent;
	struct m0_conf_cache *cache = &tx->spt_cache;

	M0_ENTRY();

	m0_tl_for(m0_conf_cache, &cache->ca_registry, obj) {
		if (m0_conf_obj_type(obj) != &M0_CONF_DIR_TYPE) {
			/* Check status*/
			if (obj->co_status != M0_CS_READY)
				return M0_ERR(-EBUSY);

			if (m0_conf_obj_type(obj) != &M0_CONF_ROOT_TYPE) {
				/* Check parent */
				if (obj->co_parent == NULL)
					return M0_ERR(-ENOENT);
				/* Check parent in cache */
				obj_parent = m0_conf_cache_lookup(cache,
							&obj->co_parent->co_id);
				if (obj_parent == NULL ||
				    obj_parent != obj->co_parent)
					return M0_ERR(-ENOENT);
			}
		}
	} m0_tl_endfor;

	return M0_RC(0);
}
M0_EXPORTED(m0_spiel_tx_validate);

/**
 * Frees Spiel context without completing the transaction.
 */
void m0_spiel_tx_close(struct m0_spiel_tx *tx)
{
	M0_ENTRY();

	m0_conf_cache_lock(&tx->spt_cache);
	/*
	 * Directories and cache objects are mixed in conf cache ca_registry
	 * list, but finalization of conf object will fail if it still
	 * presents in some directory. So delete all directories first.
	 */
	m0_conf_cache_clean(&tx->spt_cache, &M0_CONF_DIR_TYPE);
	m0_conf_cache_unlock(&tx->spt_cache);

	m0_conf_cache_fini(&tx->spt_cache);
	m0_mutex_fini(&tx->spt_lock);

	M0_LEAVE();
}
M0_EXPORTED(m0_spiel_tx_close);

static bool spiel_load_cmd_invariant(struct m0_spiel_load_command *cmd)
{
	return _0C(cmd != NULL) &&
	       _0C(cmd->slc_load_fop.f_type == &m0_fop_conf_load_fopt);
}

static uint64_t spiel_root_conf_version(struct m0_spiel_tx *tx)
{
	int                  rc;
	uint64_t             verno;
	struct m0_conf_obj  *obj;
	struct m0_conf_root *root;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);
	rc = m0_conf_obj_find(&tx->spt_cache, &M0_CONF_ROOT_FID, &obj);
	M0_ASSERT(_0C(rc == 0) && _0C(obj != NULL));
	root = M0_CONF_CAST(obj, m0_conf_root);
	verno = root->rt_verno;
	m0_mutex_unlock(&tx->spt_lock);

	return verno;
}
/**
 * Finalizes a FOP for Spiel Load command.
 * @pre spiel_load_cmd_invariant(spiel_cmd)
 */
static void spiel_load_fop_fini(struct m0_spiel_load_command *spiel_cmd)
{
	M0_PRE(spiel_load_cmd_invariant(spiel_cmd));
	m0_rpc_bulk_fini(&spiel_cmd->slc_rbulk);
	m0_fop_fini(&spiel_cmd->slc_load_fop);
}

/**
 * Release a FOP for Spiel Load command.
 * @pre spiel_load_cmd_invariant(spiel_cmd)
 */
static void spiel_load_fop_release(struct m0_ref *ref)
{
	struct m0_spiel_load_command *spiel_cmd;
	struct m0_fop                *fop;

	fop = container_of(ref, struct m0_fop, f_ref);
	spiel_cmd = container_of(fop, struct m0_spiel_load_command,
				 slc_load_fop);
	spiel_load_fop_fini(spiel_cmd);
}

/**
 * Initializes a FOP for Spiel Load command.
 * @pre spiel_cmd != NULL.
 * @pre tx != NULL.
 * @post spiel_load_cmd_invariant(spiel_cmd)
 */
static int spiel_load_fop_init(struct m0_spiel_load_command *spiel_cmd,
			       struct m0_spiel_tx           *tx)
{
	int                      rc;
	struct m0_fop_conf_load *conf_fop;

	M0_PRE(spiel_cmd != NULL);
	M0_PRE(tx != NULL);

	m0_fop_init(&spiel_cmd->slc_load_fop, &m0_fop_conf_load_fopt, NULL,
		    spiel_load_fop_release);
	rc = m0_fop_data_alloc(&spiel_cmd->slc_load_fop);

	if (rc == 0) {
		/* Fill Spiel Conf FOP specific data*/
		conf_fop = m0_conf_fop_to_load_fop(&spiel_cmd->slc_load_fop);
		conf_fop->clf_version = spiel_root_conf_version(tx);
		conf_fop->clf_tx_id = (uint64_t)tx;
		m0_rpc_bulk_init(&spiel_cmd->slc_rbulk);
		M0_POST(spiel_load_cmd_invariant(spiel_cmd));
	}
	return M0_RC(rc);
}

static int spiel_load_fop_create(struct m0_spiel_tx           *tx,
				 struct m0_spiel_load_command *spiel_cmd)
{
	int                     rc;
	struct m0_rpc_bulk_buf *rbuf;
	struct m0_net_domain   *nd;
	int                     i;
	int                     segs_nr;
	m0_bcount_t             seg_size;
	m0_bcount_t             size = strlen(tx->spt_buffer)+1;


	rc = spiel_load_fop_init(spiel_cmd, tx);
	if (rc != 0) {
		return M0_ERR(rc);
	}

	/* Fill RPC Bulk part of Spiel FOM */
	nd = spiel_rmachine(tx->spt_spiel)->rm_tm.ntm_dom;
	seg_size = m0_net_domain_get_max_buffer_segment_size(nd);
	/*
	 * Calculate number of segments for given data size.
	 * Segments number is rounded up.
	 */
	segs_nr = (size + seg_size - 1) / seg_size;
	rc = m0_rpc_bulk_buf_add(&spiel_cmd->slc_rbulk, segs_nr, nd,
				 NULL, &rbuf);
	for (i = 0; i < segs_nr; ++i) {
		m0_rpc_bulk_buf_databuf_add(rbuf,
				    tx->spt_buffer + i*seg_size,
				    min_check(size, seg_size), i, nd);
		size -= seg_size;
	}
	rbuf->bb_nbuf->nb_qtype = M0_NET_QT_PASSIVE_BULK_SEND;

	return rc;
}

static int spiel_load_fop_send(struct m0_spiel_tx           *tx,
			       struct m0_spiel_load_command *spiel_cmd)
{
	int                          rc;
	struct m0_fop_conf_load     *load_fop;
	struct m0_fop               *rep;
	struct m0_fop_conf_load_rep *load_rep;

	rc = spiel_load_fop_create(tx, spiel_cmd);
	if (rc != 0)
		return rc;

	load_fop = m0_conf_fop_to_load_fop(&spiel_cmd->slc_load_fop);
	rc = m0_rpc_bulk_store(&spiel_cmd->slc_rbulk,
			       &spiel_cmd->slc_connect,
			       &load_fop->clf_desc,
			       &m0_rpc__buf_bulk_cb);
	if (rc != 0)
		return rc;

	rc = m0_rpc_post_sync(&spiel_cmd->slc_load_fop,
			      &spiel_cmd->slc_session,
			      NULL,
			      0/*M0_TIME_NEVER*/);
	rep = rc == 0 ? m0_rpc_item_to_fop(
				spiel_cmd->slc_load_fop.f_item.ri_reply)
			: NULL;
	load_rep = rep != NULL ? m0_conf_fop_to_load_fop_rep(rep) : NULL;

	if (load_rep != NULL) {
		rc = load_rep->clfr_rc;
		spiel_cmd->slc_version = load_rep->clfr_version;
	} else {
		rc = M0_ERR(-ENOENT);
	}

	m0_fop_put_lock(&spiel_cmd->slc_load_fop);

	return rc;
}

static void spiel_flip_fop_release(struct m0_ref *ref)
{
	struct m0_fop *fop;
	fop = container_of(ref, struct m0_fop, f_ref);
	m0_fop_fini(fop);
}

static int spiel_flip_fop_send(struct m0_spiel_tx           *tx,
			       struct m0_spiel_load_command *spiel_cmd)
{
	int                          rc;
	struct m0_fop_conf_flip     *flip_fop;
	struct m0_fop               *rep;
	struct m0_fop_conf_flip_rep *flip_rep;

	M0_PRE(spiel_cmd != NULL);

	M0_ENTRY();

	m0_fop_init(&spiel_cmd->slc_flip_fop, &m0_fop_conf_flip_fopt, NULL,
		    spiel_flip_fop_release);

	rc = m0_fop_data_alloc(&spiel_cmd->slc_flip_fop);

	if (rc == 0) {
		flip_fop = m0_conf_fop_to_flip_fop(&spiel_cmd->slc_flip_fop);
		flip_fop->cff_prev_version = spiel_cmd->slc_version;
		flip_fop->cff_next_version = spiel_root_conf_version(tx);
		flip_fop->cff_tx_id = (uint64_t)tx;
	}

	if (rc == 0)
		rc = m0_rpc_post_sync(&spiel_cmd->slc_flip_fop,
				      &spiel_cmd->slc_session,
				      NULL, 0);

	rep = rc == 0 ? m0_rpc_item_to_fop(
				spiel_cmd->slc_flip_fop.f_item.ri_reply)
			: NULL;

	flip_rep = rep != NULL ? m0_conf_fop_to_flip_fop_rep(rep) : NULL;
	rc = flip_rep != NULL ? flip_rep->cffr_rc : M0_ERR(-ENOENT);
	m0_fop_put_lock(&spiel_cmd->slc_flip_fop);

	return M0_RC(rc);
}

static int  wlock_ctx_semaphore_init(struct m0_spiel_wlock_ctx *wlx)
{
	return m0_semaphore_init(&wlx->wlc_sem, 0);
}

static void wlock_ctx_semaphore_up(struct m0_spiel_wlock_ctx *wlx)
{
	m0_semaphore_up(&wlx->wlc_sem);
}

static int wlock_ctx_create(struct m0_spiel *spl)
{
	struct m0_spiel_wlock_ctx *wlx;
	int                        rc;

	M0_PRE(spl->spl_wlock_ctx == NULL);

	M0_ENTRY();
	M0_ALLOC_PTR(wlx);
	if (wlx == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto err;
	}

	wlx->wlc_rmach = spiel_rmachine(spl);
	spiel_rwlockable_write_domain_init(wlx);
	m0_rw_lockable_init(&wlx->wlc_rwlock, &M0_RWLOCK_FID, &wlx->wlc_dom);
	m0_fid_tgenerate(&wlx->wlc_owner_fid, M0_RM_OWNER_FT);
	m0_rm_rwlock_owner_init(&wlx->wlc_owner, &wlx->wlc_owner_fid,
				&wlx->wlc_rwlock, NULL);
	rc = m0_rconfc_rm_endpoint(&spl->spl_rconfc, &wlx->wlc_rm_addr);
	if (rc != 0)
		goto rwlock_alloc;
	rc = wlock_ctx_semaphore_init(wlx);
	if (rc != 0)
		goto rwlock_alloc;
	spl->spl_wlock_ctx = wlx;
	return M0_RC(0);
rwlock_alloc:
	m0_free(wlx);
err:
	return M0_ERR(rc);
}

static void wlock_ctx_destroy(struct m0_spiel_wlock_ctx *wlx)
{
	int rc;

	M0_PRE(wlx != NULL);

	M0_ENTRY("wlock ctx %p", wlx);
	m0_rm_owner_windup(&wlx->wlc_owner);
	rc = m0_rm_owner_timedwait(&wlx->wlc_owner,
				   M0_BITS(ROS_FINAL, ROS_INSOLVENT),
				   M0_TIME_NEVER);
	M0_ASSERT(rc == 0);
	M0_LOG(M0_DEBUG, "owner winduped");
	m0_rm_rwlock_owner_fini(&wlx->wlc_owner);
	m0_rw_lockable_fini(&wlx->wlc_rwlock);
	spiel_rwlockable_write_domain_fini(wlx);
	M0_LEAVE();
}

static int wlock_ctx_connect(struct m0_spiel_wlock_ctx *wlx)
{
	enum { MAX_RPCS_IN_FLIGHT = 15 };

	M0_PRE(wlx != NULL);
	return m0_rpc_client_connect(&wlx->wlc_conn, &wlx->wlc_sess,
				     wlx->wlc_rmach, wlx->wlc_rm_addr,
				     NULL, MAX_RPCS_IN_FLIGHT,
				     M0_TIME_NEVER);
}

static void wlock_ctx_disconnect(struct m0_spiel_wlock_ctx *wlx)
{
	int               rc;

	M0_PRE(_0C(wlx != NULL) && _0C(!M0_IS0(&wlx->wlc_sess)));
	M0_ENTRY("wlock ctx %p", wlx);
	rc = m0_rpc_session_destroy(&wlx->wlc_sess, M0_TIME_NEVER);
	if (rc != 0)
		M0_LOG(M0_ERROR, "Failed to destroy wlock session");
	rc = m0_rpc_conn_destroy(&wlx->wlc_conn, M0_TIME_NEVER);
	if (rc != 0)
		M0_LOG(M0_ERROR, "Failed to destroy wlock connection");
	M0_LEAVE();

}

static void spiel_tx_write_lock_complete(struct m0_rm_incoming *in,
					 int32_t                rc)
{
	struct m0_spiel_wlock_ctx *wlx = container_of(in,
						      struct m0_spiel_wlock_ctx,
						      wlc_req);

	M0_ENTRY("incoming %p, rc %d", in, rc);
	wlx->wlc_rc = rc;
	wlock_ctx_semaphore_up(wlx);
	M0_LEAVE();
}

static void spiel_tx_write_lock_conflict(struct m0_rm_incoming *in)
{
	/* Do nothing */
}

static struct m0_rm_incoming_ops spiel_tx_ri_ops = {
	.rio_complete = spiel_tx_write_lock_complete,
	.rio_conflict = spiel_tx_write_lock_conflict,
};

static void wlock_ctx_creditor_setup(struct m0_spiel_wlock_ctx *wlx)
{
	struct m0_rm_owner    *owner;
	struct m0_rm_remote   *creditor;

	M0_PRE(wlx != NULL);
	M0_ENTRY("wlx = %p", wlx);
	owner = &wlx->wlc_owner;
	creditor = &wlx->wlc_creditor;
	m0_rm_remote_init(creditor, owner->ro_resource);
	creditor->rem_session = &wlx->wlc_sess;
	m0_rm_owner_creditor_reset(owner, creditor);
	M0_LEAVE();
}

static void _spiel_tx_write_lock_get(struct m0_spiel_wlock_ctx *wlx)
{
	struct m0_rm_incoming *req;

	M0_PRE(wlx != NULL);
	M0_ENTRY("wlock ctx = %p", wlx);
	req = &wlx->wlc_req;
	m0_rm_rwlock_req_init(req, &wlx->wlc_owner, &spiel_tx_ri_ops,
			      RIF_MAY_BORROW | RIF_MAY_REVOKE | RIF_LOCAL_WAIT,
			      RM_RWLOCK_WRITE);
	m0_rm_credit_get(req);
	M0_LEAVE();
}

static void wlock_ctx_creditor_unset(struct m0_spiel_wlock_ctx *wlx)
{
	M0_PRE(wlx != NULL);
	M0_ENTRY("wlx = %p", wlx);
	m0_rm_remote_fini(&wlx->wlc_creditor);
	wlx->wlc_owner.ro_creditor = NULL;
	M0_LEAVE();
}

static void wlock_ctx_semaphore_down(struct m0_spiel_wlock_ctx *wlx)
{
	m0_semaphore_down(&wlx->wlc_sem);
}

static int spiel_tx_write_lock_get(struct m0_spiel_tx *tx)
{
	struct m0_spiel           *spl;
	struct m0_spiel_wlock_ctx *wlx;
	int                        rc;

	M0_PRE(tx != NULL);
	M0_ENTRY("tx %p", tx);

	spl = tx->spt_spiel;
	rc = wlock_ctx_create(spl);
	if (rc != 0)
		goto fail;
	wlx = spl->spl_wlock_ctx;
	rc = wlock_ctx_connect(wlx);
	if (rc != 0)
		goto ctx_free;
	wlock_ctx_creditor_setup(wlx);
	_spiel_tx_write_lock_get(wlx);
	wlock_ctx_semaphore_down(wlx);
	rc = wlx->wlc_rc;
	if (rc != 0)
		goto ctx_destroy;
	return M0_RC(0);
ctx_destroy:
	wlock_ctx_destroy(wlx);
	wlock_ctx_disconnect(wlx);
ctx_free:
	m0_free(wlx->wlc_rm_addr);
	m0_free(wlx);
fail:
	return M0_ERR(rc);
}

static void _spiel_tx_write_lock_put(struct m0_spiel_wlock_ctx *wlx)
{
	struct m0_rm_incoming *req;

	M0_PRE(wlx != NULL);
	M0_ENTRY("wlock ctx = %p", wlx);
	req = &wlx->wlc_req;
	m0_rm_credit_put(req);
	m0_rm_incoming_fini(req);
	wlock_ctx_creditor_unset(wlx);
	M0_LEAVE();
}

static void spiel_tx_write_lock_put(struct m0_spiel_tx *tx)
{
	struct m0_spiel           *spl;
	struct m0_spiel_wlock_ctx *wlx;

	M0_PRE(tx != NULL);
	M0_ENTRY("tx %p", tx);

	spl = tx->spt_spiel;
	wlx = spl->spl_wlock_ctx;
	_spiel_tx_write_lock_put(spl->spl_wlock_ctx);
	wlock_ctx_destroy(spl->spl_wlock_ctx);
	wlock_ctx_disconnect(spl->spl_wlock_ctx);
	m0_free(wlx->wlc_rm_addr);
	m0_free(wlx);
	M0_LEAVE();
}

int m0_spiel_tx_commit_forced(struct m0_spiel_tx *tx,
			      bool                forced,
			      uint64_t            ver_forced,
			      uint32_t           *rquorum)
{
	enum { MAX_RPCS_IN_FLIGHT = 2 };
	int                            rc;
	struct m0_spiel_load_command  *spiel_cmd   = NULL;
	const char                   **confd_eps   = NULL;
	uint32_t                       confd_count = 0;
	uint32_t                       quorum      = 0;
	uint32_t                       idx;
	uint64_t                       rconfc_ver;

	M0_ENTRY();
	rc = m0_spiel_rconfc_start(tx->spt_spiel, NULL);
	if (rc != 0)
		goto rconfc_fail;
	/*
	 * in case ver_forced value is other than M0_CONF_VER_UNKNOWN, override
	 * transaction version number with ver_forced, otherwise leave the one
	 * intact
	 */
	if (ver_forced != M0_CONF_VER_UNKNOWN) {
		rc = spiel_root_ver_update(tx, ver_forced);
		M0_ASSERT(rc == 0);
	} else {
		rconfc_ver = m0_rconfc_ver_max_read(&tx->spt_spiel->spl_rconfc);
		if (rconfc_ver != M0_CONF_VER_UNKNOWN) {
			++rconfc_ver;
		} else {
			/*
			 * Version number may be unknown due to cluster quorum
			 * issues at runtime, so need to return error code to
			 * client.
			 */
			rc = -ENODATA;
			goto tx_fini;
		}
		rc = spiel_root_ver_update(tx, rconfc_ver);
		M0_ASSERT(rc == 0);
	}

	rc = m0_spiel_tx_validate(tx) ?:
	     m0_conf_cache_to_string(&tx->spt_cache, &tx->spt_buffer, false);
	if (M0_FI_ENABLED("encode_fail"))
		rc = -ENOMEM;
	if (rc != 0)
		goto tx_fini;

	confd_count = m0_rconfc_confd_endpoints(&tx->spt_spiel->spl_rconfc,
						&confd_eps);
	if  (confd_count == 0) {
		rc = M0_ERR(-ENODATA);
		goto tx_fini;
	} else if (confd_count < 0) {
		rc = M0_ERR(confd_count);
		goto tx_fini;
	}
	if (!M0_FI_ENABLED("cmd_alloc_fail"))
		M0_ALLOC_ARR(spiel_cmd, confd_count);
	if (spiel_cmd == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto tx_fini;
	}
	for (idx = 0; idx < confd_count; ++idx) {
		spiel_cmd[idx].slc_status =
			m0_rpc_client_connect(&spiel_cmd[idx].slc_connect,
					      &spiel_cmd[idx].slc_session,
					      spiel_rmachine(tx->spt_spiel),
					      confd_eps[idx], NULL,
					      MAX_RPCS_IN_FLIGHT,
					      M0_TIME_NEVER) ?:
			spiel_load_fop_send(tx, &spiel_cmd[idx]);
	}

	quorum = m0_count(idx, confd_count, (spiel_cmd[idx].slc_status == 0));
	/*
	 * Unless forced transaction committing requested, make sure the quorum
	 * at least reached the value specified at m0_spiel_start_quorum(), or
	 * better went beyond it.
	 */
	if (!forced && quorum < tx->spt_spiel->spl_rconfc.rc_quorum) {
		rc = M0_ERR(-ENOENT);
		goto tx_fini;
	}

	quorum = 0;
	rc = spiel_tx_write_lock_get(tx);
	if (rc != 0)
		goto tx_fini;
	for (idx = 0; idx < confd_count; ++idx) {
		if (spiel_cmd[idx].slc_status == 0) {
			rc = spiel_flip_fop_send(tx, &spiel_cmd[idx]);
			if (rc == 0)
				++quorum;
		}
	}
	/* TODO: handle creditor death */
	spiel_tx_write_lock_put(tx);
	rc = !forced && quorum < tx->spt_spiel->spl_rconfc.rc_quorum ?
		M0_ERR(-ENOENT) : 0;

tx_fini:
	m0_spiel_rconfc_stop(tx->spt_spiel);
rconfc_fail:
	m0_strings_free(confd_eps);
	if (tx->spt_buffer != NULL)
		m0_confx_string_free(tx->spt_buffer);

	if (spiel_cmd != NULL) {
		for (idx = 0; idx < confd_count; ++idx) {
			struct m0_rpc_session *ss = &spiel_cmd[idx].slc_session;
			struct m0_rpc_conn    *sc = &spiel_cmd[idx].slc_connect;

			if (!M0_IN(ss->s_sm.sm_state,
				   (M0_RPC_SESSION_FAILED,
				    M0_RPC_SESSION_INITIALISED)))
				m0_rpc_session_destroy(ss, M0_TIME_NEVER);
			if (!M0_IN(sc->c_sm.sm_state,
				   (M0_RPC_CONN_FAILED,
				    M0_RPC_CONN_INITIALISED)))
			    m0_rpc_conn_destroy(sc, M0_TIME_NEVER);
		}
		m0_free(spiel_cmd);
	}

	/* report resultant quorum value reached during committing */
	if (rquorum != NULL)
		*rquorum = quorum;

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_tx_commit_forced);

int m0_spiel_tx_commit(struct m0_spiel_tx  *tx)
{
	return m0_spiel_tx_commit_forced(tx, false, M0_CONF_VER_UNKNOWN, NULL);
}
M0_EXPORTED(m0_spiel_tx_commit);

/**
 * Create m0_conf_dir objects for m0_conf_filesystem
 */
static int spiel_filesystem_dirs_create(struct m0_conf_cache      *cache,
					struct m0_conf_filesystem *fs)
{
	return M0_RC(dir_new_adopt(cache, &fs->cf_obj,
				   &M0_CONF_FILESYSTEM_NODES_FID,
				   &M0_CONF_NODE_TYPE, NULL, &fs->cf_nodes) ?:
		     dir_new_adopt(cache, &fs->cf_obj,
				   &M0_CONF_FILESYSTEM_POOLS_FID,
				   &M0_CONF_POOL_TYPE, NULL, &fs->cf_pools) ?:
		     dir_new_adopt(cache, &fs->cf_obj,
				   &M0_CONF_FILESYSTEM_RACKS_FID,
				   &M0_CONF_RACK_TYPE, NULL, &fs->cf_racks));
}

/**
 * Create m0_conf_dir objects for m0_conf_node
 */
static int spiel_node_dirs_create(struct m0_conf_cache *cache,
				  struct m0_conf_node  *node)
{
	return M0_RC(dir_new_adopt(cache, &node->cn_obj,
				   &M0_CONF_NODE_PROCESSES_FID,
				   &M0_CONF_PROCESS_TYPE, NULL,
				   &node->cn_processes));
}

/**
 * Create m0_conf_dir objects for m0_conf_process
 */
static int spiel_process_dirs_create(struct m0_conf_cache   *cache,
				     struct m0_conf_process *process)
{
	return M0_RC(dir_new_adopt(cache, &process->pc_obj,
				   &M0_CONF_PROCESS_SERVICES_FID,
				   &M0_CONF_SERVICE_TYPE, NULL,
				   &process->pc_services));
}

/**
 * Create m0_conf_dir objects for m0_conf_service
 */
static int spiel_service_dirs_create(struct m0_conf_cache   *cache,
				     struct m0_conf_service *service)
{
	return M0_RC(dir_new_adopt(cache, &service->cs_obj,
				   &M0_CONF_SERVICE_SDEVS_FID,
				   &M0_CONF_SDEV_TYPE, NULL,
				   &service->cs_sdevs));
}

/**
 * Create m0_conf_dir objects for m0_conf_pool
 */
static int spiel_pool_dirs_create(struct m0_conf_cache *cache,
				  struct m0_conf_pool  *pool)
{
	return M0_RC(dir_new_adopt(cache, &pool->pl_obj,
				   &M0_CONF_POOL_PVERS_FID, &M0_CONF_PVER_TYPE,
				   NULL, &pool->pl_pvers));
}

/**
 * Create m0_conf_dir objects for m0_conf_rack
 */
static int spiel_rack_dirs_create(struct m0_conf_cache *cache,
				  struct m0_conf_rack  *rack)
{
	return M0_RC(dir_new_adopt(cache, &rack->cr_obj,
				   &M0_CONF_RACK_ENCLS_FID,
				   &M0_CONF_ENCLOSURE_TYPE, NULL,
				   &rack->cr_encls));
}

/**
 * Create m0_conf_dir objects for m0_conf_enclosure
 */
static int spiel_enclosure_dirs_create(struct m0_conf_cache     *cache,
				       struct m0_conf_enclosure *enclosure)
{
	return M0_RC(dir_new_adopt(cache, &enclosure->ce_obj,
				   &M0_CONF_ENCLOSURE_CTRLS_FID,
				   &M0_CONF_CONTROLLER_TYPE, NULL,
				   &enclosure->ce_ctrls));
}

/**
 * Create m0_conf_dir objects for m0_conf_controller
 */
static int spiel_controller_dirs_create(struct m0_conf_cache      *cache,
					struct m0_conf_controller *controller)
{
	return M0_RC(dir_new_adopt(cache, &controller->cc_obj,
				   &M0_CONF_CONTROLLER_DISKS_FID,
				   &M0_CONF_DISK_TYPE, NULL,
				   &controller->cc_disks));
}

/**
 * Create m0_conf_dir objects for m0_conf_pver
 */
static int spiel_pver_dirs_create(struct m0_conf_cache *cache,
				  struct m0_conf_pver  *pver)
{
	return M0_RC(dir_new_adopt(cache, &pver->pv_obj,
				   &M0_CONF_PVER_RACKVS_FID, &M0_CONF_OBJV_TYPE,
				   NULL, &pver->pv_rackvs));
}

/**
 * Create m0_conf_dir objects for rack version
 */
static int spiel_rackv_dirs_create(struct m0_conf_cache *cache,
				   struct m0_conf_objv  *objv)
{
	return M0_RC(dir_new_adopt(cache, &objv->cv_obj,
				   &M0_CONF_RACKV_ENCLVS_FID,
				   &M0_CONF_OBJV_TYPE, NULL,
				   &objv->cv_children));
}

/**
 * Create m0_conf_dir objects for enclosure version
 */
static int spiel_enclosurev_dirs_create(struct m0_conf_cache *cache,
					struct m0_conf_objv  *objv)
{
	return M0_RC(dir_new_adopt(cache, &objv->cv_obj,
				   &M0_CONF_ENCLV_CTRLVS_FID,
				   &M0_CONF_OBJV_TYPE, NULL,
				   &objv->cv_children));
}

/**
 * Create m0_conf_dir objects for controller version
 */
static int spiel_controllerv_dirs_create(struct m0_conf_cache *cache,
					struct m0_conf_objv   *objv)
{
	return M0_RC(dir_new_adopt(cache, &objv->cv_obj,
				   &M0_CONF_CTRLV_DISKVS_FID,
				   &M0_CONF_OBJV_TYPE, NULL,
				   &objv->cv_children));
}

static int spiel_conf_parameter_check(struct m0_conf_cache    *cache,
				      struct spiel_conf_param *parameters)
{
	int                      rc;
	struct spiel_conf_param *param;

	M0_PRE(cache != NULL);
	M0_PRE(parameters != NULL);

	for (param = parameters; param->scp_type != NULL; ++param) {
		if (param->scp_fid == NULL ||
		    m0_conf_fid_type(param->scp_fid) != param->scp_type)
			return M0_ERR(-EINVAL);
		rc = m0_conf_obj_find(cache, param->scp_fid, param->scp_obj);
		if (rc != 0)
			return M0_ERR(rc);
	}
	return (*parameters->scp_obj)->co_status == M0_CS_MISSING ? M0_RC(0) :
		M0_ERR(-EEXIST);
}

int m0_spiel_profile_add(struct m0_spiel_tx *tx, const struct m0_fid *fid)
{
	int                  rc;
	struct m0_conf_obj  *obj = NULL;
	struct m0_conf_obj  *obj_parent;
	struct m0_conf_root *root;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_PROFILE_TYPE, &obj},
			      {&M0_CONF_ROOT_FID, &M0_CONF_ROOT_TYPE,
			       &obj_parent});
	if (rc != 0)
		goto fail;

	root = M0_CONF_CAST(obj_parent, m0_conf_root);
	m0_conf_dir_add(root->rt_profiles, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_profile_add);

int m0_spiel_filesystem_add(struct m0_spiel_tx   *tx,
			    const struct m0_fid  *fid,
			    const struct m0_fid  *parent,
			    unsigned              redundancy,
			    const struct m0_fid  *rootfid,
			    const struct m0_fid  *mdpool,
			    const char          **fs_params)
{
	int                        rc;
	struct m0_conf_obj        *obj = NULL;
	struct m0_conf_filesystem *fs;
	struct m0_conf_obj        *obj_parent;
	struct m0_conf_obj        *pool;

	M0_ENTRY();
	if (fs_params == NULL || rootfid == NULL)
		return M0_ERR(-EINVAL);
	/*
	 * TODO: rootfid may have any type at the moment since it is not used
	 * yet. But it may be changed and here need to check fid type in the
	 * future.
	 */
	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_FILESYSTEM_TYPE, &obj},
			      {parent, &M0_CONF_PROFILE_TYPE, &obj_parent},
			      {mdpool, &M0_CONF_POOL_TYPE, &pool});
	if (rc != 0)
		goto fail;

	fs = M0_CONF_CAST(obj, m0_conf_filesystem);
	fs->cf_params = m0_strings_dup(fs_params);
	if (fs->cf_params == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto fail;
	}
	/* XXX FIXME: fs->cf_params will leak in case of error */
	fs->cf_rootfid = *rootfid;
	fs->cf_mdpool = *mdpool;
	fs->cf_redundancy = redundancy;

	rc = spiel_filesystem_dirs_create(&tx->spt_cache, fs);
	if (rc != 0)
		goto fail;
	M0_CONF_CAST(obj_parent, m0_conf_profile)->cp_filesystem = fs;
	child_adopt(obj_parent, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_filesystem_add);


int m0_spiel_node_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent,
		      uint32_t             memsize,
		      uint32_t             nr_cpu,
		      uint64_t             last_state,
		      uint64_t             flags,
		      struct m0_fid       *pool_fid)
{
	int                        rc;
	struct m0_conf_obj        *obj = NULL;
	struct m0_conf_obj        *obj_parent;
	struct m0_conf_node       *node;
	struct m0_conf_obj        *pool;
	struct m0_conf_filesystem *fs;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_NODE_TYPE, &obj },
			      {pool_fid, &M0_CONF_POOL_TYPE, &pool},
			      {parent, &M0_CONF_FILESYSTEM_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	node = M0_CONF_CAST(obj, m0_conf_node);
	node->cn_memsize = memsize;
	node->cn_nr_cpu = nr_cpu;
	node->cn_last_state = last_state;
	node->cn_flags = flags;
	node->cn_pool = M0_CONF_CAST(pool, m0_conf_pool);

	rc = spiel_node_dirs_create(&tx->spt_cache, node);
	if (rc != 0)
		goto fail;
	fs = M0_CONF_CAST(obj_parent, m0_conf_filesystem);
	if (fs->cf_nodes == NULL) {
		/* Parent dir does not exist ==> create it. */
		rc = spiel_filesystem_dirs_create(&tx->spt_cache, fs);
		if (rc != 0)
			goto fail;
	}
	m0_conf_dir_add(fs->cf_nodes, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_node_add);

static bool spiel_cores_is_valid(const struct m0_bitmap *cores)
{
	return cores != NULL && m0_exists(i, cores->b_nr,
					  cores->b_words[i] != 0);
}

int m0_spiel_process_add(struct m0_spiel_tx  *tx,
			 const struct m0_fid *fid,
			 const struct m0_fid *parent,
			 struct m0_bitmap    *cores,
			 uint64_t             memlimit_as,
			 uint64_t             memlimit_rss,
			 uint64_t             memlimit_stack,
			 uint64_t             memlimit_memlock,
			 const char          *endpoint)
{
	int                     rc;
	struct m0_conf_obj     *obj = NULL;
	struct m0_conf_process *process;
	struct m0_conf_obj     *obj_parent;
	struct m0_conf_node    *node;

	M0_ENTRY();
	if (!spiel_cores_is_valid(cores) || endpoint == NULL)
		return M0_ERR(-EINVAL);
	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_PROCESS_TYPE, &obj },
			      {parent, &M0_CONF_NODE_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	process = M0_CONF_CAST(obj, m0_conf_process);
	rc = m0_bitmap_init(&process->pc_cores, cores->b_nr);
	if (rc != 0)
		goto fail;
	/* XXX FIXME: process->pc_cores.b_words will leak in case of error */
	m0_bitmap_copy(&process->pc_cores, cores);
	process->pc_memlimit_as      = memlimit_as;
	process->pc_memlimit_rss     = memlimit_rss;
	process->pc_memlimit_stack   = memlimit_stack;
	process->pc_memlimit_memlock = memlimit_memlock;
	process->pc_endpoint         = m0_strdup(endpoint);
	if (process->pc_endpoint == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto fail;
	}
	/* XXX FIXME: process->pc_endpoint will leak in case of error */

	rc = spiel_process_dirs_create(&tx->spt_cache, process);
	if (rc != 0)
		goto fail;
	node = M0_CONF_CAST(obj_parent, m0_conf_node);
	if (node->cn_processes == NULL) {
		/* Parent dir does not exist ==> create it. */
		rc = spiel_node_dirs_create(&tx->spt_cache, node);
		if (rc != 0)
			goto fail;
	}
	m0_conf_dir_add(node->cn_processes, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_process_add);

static int spiel_service_info_copy(struct m0_conf_service             *service,
				   const struct m0_spiel_service_info *info)
{
	if (!m0_conf_service_type_is_valid(info->svi_type))
		return M0_ERR(-EINVAL);

	if (info->svi_type == M0_CST_MGS && info->svi_u.confdb_path == NULL)
		return M0_ERR(-EINVAL);

	/* TODO: Check what parameters are used by which service types */
	memcpy(&service->cs_u, &info->svi_u, sizeof(service->cs_u));

	if (info->svi_type == M0_CST_MGS) {
		service->cs_u.confdb_path = m0_strdup(info->svi_u.confdb_path);
		if (service->cs_u.confdb_path == NULL)
			return M0_ERR(-ENOMEM);
	}
	return M0_RC(0);
}

int m0_spiel_service_add(struct m0_spiel_tx                 *tx,
			 const struct m0_fid                *fid,
			 const struct m0_fid                *parent,
			 const struct m0_spiel_service_info *service_info)
{
	int                     rc;
	struct m0_conf_obj     *obj = NULL;
	struct m0_conf_service *service;
	struct m0_conf_obj     *obj_parent;
	struct m0_conf_process *process;

	M0_ENTRY();
	if (service_info == NULL)
		return M0_ERR(-EINVAL);
	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_SERVICE_TYPE, &obj },
			      {parent, &M0_CONF_PROCESS_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	service = M0_CONF_CAST(obj, m0_conf_service);
	service->cs_type = service_info->svi_type;
	service->cs_endpoints = m0_strings_dup(service_info->svi_endpoints);
	if (service->cs_endpoints == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto fail;
	}
	/* XXX FIXME: service->cs_endpoints will leak in case of error */

	/* Copy Different service-specific parameters */
	rc = spiel_service_info_copy(service, service_info);
	if (rc != 0)
		goto fail;

	rc = spiel_service_dirs_create(&tx->spt_cache, service);
	if (rc != 0)
		goto fail;
	process = M0_CONF_CAST(obj_parent, m0_conf_process);
	if (process->pc_services == NULL) {
		/* Parent dir does not exist ==> create it. */
		rc = spiel_process_dirs_create(&tx->spt_cache, process);
		if (rc != 0)
			goto fail;
	}
	m0_conf_dir_add(process->pc_services, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_service_add);

int m0_spiel_device_add(struct m0_spiel_tx                        *tx,
			const struct m0_fid                       *fid,
			const struct m0_fid                       *svc_parent,
			const struct m0_fid                       *disk_parent,
		        uint32_t                                   dev_idx,
			enum m0_cfg_storage_device_interface_type  iface,
			enum m0_cfg_storage_device_media_type      media,
			uint32_t                                   bsize,
			uint64_t                                   size,
			uint64_t                                   last_state,
			uint64_t                                   flags,
			const char                                *filename)
{
	int                     rc;
	struct m0_conf_obj     *obj = NULL;
	struct m0_conf_sdev    *device;
	struct m0_conf_obj     *svc_obj;
	struct m0_conf_obj     *disk_obj;
	struct m0_conf_service *service;
	struct m0_conf_disk    *disk;

	M0_ENTRY();
	if (dev_idx > M0_FID_DEVICE_ID_MAX ||
	    !M0_CFG_SDEV_INTERFACE_TYPE_IS_VALID(iface) ||
	    !M0_CFG_SDEV_MEDIA_TYPE_IS_VALID(media) ||
	    filename == NULL)
		return M0_ERR(-EINVAL);

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_SDEV_TYPE, &obj },
			      {svc_parent, &M0_CONF_SERVICE_TYPE, &svc_obj},
			      {disk_parent, &M0_CONF_DISK_TYPE, &disk_obj});
	if (rc != 0)
		goto fail;

	device = M0_CONF_CAST(obj, m0_conf_sdev);
	device->sd_dev_idx = dev_idx;
	device->sd_iface = iface;
	device->sd_media = media;
	device->sd_bsize = bsize;
	device->sd_size = size;
	device->sd_last_state = last_state;
	device->sd_flags = flags;
	device->sd_filename = m0_strdup(filename);
	if (device->sd_filename == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto fail;
	}
	/* XXX FIXME: device->sd_filename will leak in case of error */

	service = M0_CONF_CAST(svc_obj, m0_conf_service);
	if (service->cs_sdevs == NULL) {
		/* Parent dir does not exist ==> create it. */
		rc = spiel_service_dirs_create(&tx->spt_cache, service);
		if (rc != 0)
			goto fail;
	}
	m0_conf_dir_add(service->cs_sdevs, obj);
	disk = M0_CONF_CAST(disk_obj, m0_conf_disk);
	if (disk->ck_dev != NULL) {
		m0_conf_dir_del(service->cs_sdevs, obj);
		rc = M0_ERR(-EINVAL);
		goto fail;
	}
	disk->ck_dev = device;
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_device_add);

int m0_spiel_pool_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent,
		      uint32_t order)
{
	int                        rc;
	struct m0_conf_obj        *obj = NULL;
	struct m0_conf_pool       *pool;
	struct m0_conf_obj        *obj_parent;
	struct m0_conf_filesystem *fs;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_POOL_TYPE, &obj },
			      {parent, &M0_CONF_FILESYSTEM_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	pool = M0_CONF_CAST(obj, m0_conf_pool);
	pool->pl_order = order;
	rc = spiel_pool_dirs_create(&tx->spt_cache, pool);
	if (rc != 0)
		goto fail;
	fs = M0_CONF_CAST(obj_parent, m0_conf_filesystem);
	if (fs->cf_pools == NULL) {
		/* Parent dir does not exist ==> create it. */
		rc = spiel_filesystem_dirs_create(&tx->spt_cache, fs);
		if (rc != 0)
			goto fail;
	}
	m0_conf_dir_add(fs->cf_pools, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_pool_add);

int m0_spiel_rack_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent)
{
	int                       rc;
	struct m0_conf_obj       *obj = NULL;
	struct m0_conf_rack       *rack;
	struct m0_conf_obj        *obj_parent;
	struct m0_conf_filesystem *fs;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_RACK_TYPE, &obj },
			      {parent, &M0_CONF_FILESYSTEM_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	rack = M0_CONF_CAST(obj, m0_conf_rack);
	rc = spiel_rack_dirs_create(&tx->spt_cache, rack);
	if (rc != 0)
		goto fail;
	fs = M0_CONF_CAST(obj_parent, m0_conf_filesystem);
	if (fs->cf_racks == NULL) {
		/* Parent dir does not exist ==> create it. */
		rc = spiel_filesystem_dirs_create(&tx->spt_cache, fs);
		if (rc != 0)
			goto fail;
	}
	m0_conf_dir_add(fs->cf_racks, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_rack_add);

int m0_spiel_enclosure_add(struct m0_spiel_tx  *tx,
			   const struct m0_fid *fid,
			   const struct m0_fid *parent)
{
	int                       rc;
	struct m0_conf_obj       *obj = NULL;
	struct m0_conf_enclosure *enclosure;
	struct m0_conf_obj       *obj_parent;
	struct m0_conf_rack      *rack;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_ENCLOSURE_TYPE, &obj },
			      {parent, &M0_CONF_RACK_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	enclosure = M0_CONF_CAST(obj, m0_conf_enclosure);
	rc = spiel_enclosure_dirs_create(&tx->spt_cache, enclosure);
	if (rc != 0)
		goto fail;
	rack = M0_CONF_CAST(obj_parent, m0_conf_rack);
	if (rack->cr_encls == NULL) {
		/* Parent dir does not exist ==> create it. */
		rc = spiel_rack_dirs_create(&tx->spt_cache, rack);
		if (rc != 0)
			goto fail;
	}
	m0_conf_dir_add(rack->cr_encls, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_enclosure_add);

int m0_spiel_controller_add(struct m0_spiel_tx  *tx,
			    const struct m0_fid *fid,
			    const struct m0_fid *parent,
			    const struct m0_fid *node)
{
	int                        rc;
	struct m0_conf_obj        *obj = NULL;
	struct m0_conf_obj        *node_obj;
	struct m0_conf_controller *controller;
	struct m0_conf_obj        *obj_parent;
	struct m0_conf_enclosure  *enclosure;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_CONTROLLER_TYPE, &obj },
			      {parent, &M0_CONF_ENCLOSURE_TYPE, &obj_parent},
			      {node, &M0_CONF_NODE_TYPE, &node_obj});
	if (rc != 0)
		goto fail;

	controller = M0_CONF_CAST(obj, m0_conf_controller);
	controller->cc_node = M0_CONF_CAST(node_obj, m0_conf_node);
	rc = spiel_controller_dirs_create(&tx->spt_cache, controller);
	if (rc != 0)
		goto fail;
	enclosure = M0_CONF_CAST(obj_parent, m0_conf_enclosure);
	if (enclosure->ce_ctrls == NULL) {
		/* Parent dir does not exist ==> create it. */
		rc = spiel_enclosure_dirs_create(&tx->spt_cache, enclosure);
		if (rc != 0)
			goto fail;
	}
	m0_conf_dir_add(enclosure->ce_ctrls, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_controller_add);

int m0_spiel_disk_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent)
{
	int                        rc;
	struct m0_conf_obj        *obj = NULL;
	struct m0_conf_obj        *obj_parent;
	struct m0_conf_controller *controller;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_DISK_TYPE, &obj },
			      {parent, &M0_CONF_CONTROLLER_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	controller = M0_CONF_CAST(obj_parent, m0_conf_controller);
	if (controller->cc_disks == NULL) {
		/* Parent dir does not exist ==> create it. */
		rc = spiel_controller_dirs_create(&tx->spt_cache, controller);
		if (rc != 0)
			goto fail;
	}
	m0_conf_dir_add(controller->cc_disks, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_disk_add);

int m0_spiel_pool_version_add(struct m0_spiel_tx     *tx,
			      const struct m0_fid    *fid,
			      const struct m0_fid    *parent,
			      uint32_t               *nr_failures,
			      uint32_t                nr_failures_cnt,
			      struct m0_pdclust_attr *attrs)
{
	int                  rc;
	struct m0_conf_obj  *obj = NULL;
	struct m0_conf_pver *pver = NULL;
	struct m0_conf_obj  *obj_parent;
	struct m0_conf_pool *pool;

	M0_ENTRY();
	if (!m0_pdclust_attr_check(attrs))
		return M0_ERR(-EINVAL);
	if (nr_failures_cnt == 0)
		return M0_ERR(-EINVAL);

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_PVER_TYPE, &obj },
			      {parent, &M0_CONF_POOL_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	pver = M0_CONF_CAST(obj, m0_conf_pver);

	pver->pv_nr_failures_nr = nr_failures_cnt;
	M0_ALLOC_ARR(pver->pv_nr_failures, nr_failures_cnt);
	if (pver->pv_nr_failures == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto fail;
	}
	memcpy(pver->pv_nr_failures, nr_failures,
	       nr_failures_cnt * sizeof(*nr_failures));
	pver->pv_attr = *attrs;

	rc = spiel_pver_dirs_create(&tx->spt_cache, pver);
	if (rc != 0)
		goto fail;
	pool = M0_CONF_CAST(obj_parent, m0_conf_pool);
	if (pool->pl_pvers == NULL) {
		/* Parent dir does not exist ==> create it. */
		rc = spiel_pool_dirs_create(&tx->spt_cache, pool);
		if (rc != 0)
			goto fail;
	}
	m0_conf_dir_add(pool->pl_pvers, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (pver != NULL)
		m0_free(pver->pv_nr_failures);
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_pool_version_add);

int m0_spiel_rack_v_add(struct m0_spiel_tx  *tx,
			const struct m0_fid *fid,
			const struct m0_fid *parent,
			const struct m0_fid *real)
{
	int                  rc;
	struct m0_conf_obj  *obj = NULL;
	struct m0_conf_objv *objv;
	struct m0_conf_obj  *obj_parent;
	struct m0_conf_pver *pver;
	struct m0_conf_obj  *real_obj;

	M0_ENTRY();

	real_obj = m0_conf_cache_lookup(&tx->spt_cache, real);
	if (real_obj == NULL)
		return M0_RC(-ENOENT);

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_OBJV_TYPE, &obj },
			      {parent, &M0_CONF_PVER_TYPE, &obj_parent},
			      {real, &M0_CONF_RACK_TYPE, &real_obj});
	if (rc != 0)
		goto fail;

	objv = M0_CONF_CAST(obj, m0_conf_objv);
	objv->cv_real = real_obj;
	rc = spiel_rackv_dirs_create(&tx->spt_cache, objv);
	if (rc != 0)
		goto fail;
	pver = M0_CONF_CAST(obj_parent, m0_conf_pver);
	if (pver->pv_rackvs == NULL) {
		/* Parent dir does not exist ==> create it. */
		rc = spiel_pver_dirs_create(&tx->spt_cache, pver);
		if (rc != 0)
			goto fail;
	}
	m0_conf_dir_add(pver->pv_rackvs, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_rack_v_add);

int m0_spiel_enclosure_v_add(struct m0_spiel_tx  *tx,
			     const struct m0_fid *fid,
			     const struct m0_fid *parent,
			     const struct m0_fid *real)
{
	int                  rc;
	struct m0_conf_obj  *obj = NULL;
	struct m0_conf_objv *objv;
	struct m0_conf_obj  *obj_parent;
	struct m0_conf_objv *objv_parent;
	struct m0_conf_obj  *real_obj;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_OBJV_TYPE, &obj },
			      {parent, &M0_CONF_OBJV_TYPE, &obj_parent},
			      {real, &M0_CONF_ENCLOSURE_TYPE, &real_obj});
	if (rc != 0)
		goto fail;

	objv = M0_CONF_CAST(obj, m0_conf_objv);
	objv->cv_real = real_obj;
	rc = spiel_enclosurev_dirs_create(&tx->spt_cache, objv);
	if (rc != 0)
		goto fail;
	objv_parent = M0_CONF_CAST(obj_parent, m0_conf_objv);
	if (objv_parent->cv_children == NULL) {
		/* Parent dir does not exist ==> create it. */
		rc = spiel_rackv_dirs_create(&tx->spt_cache, objv_parent);
		if (rc != 0)
			goto fail;
	}
	m0_conf_dir_add(objv_parent->cv_children, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_enclosure_v_add);

int m0_spiel_controller_v_add(struct m0_spiel_tx  *tx,
			      const struct m0_fid *fid,
			      const struct m0_fid *parent,
			      const struct m0_fid *real)
{
	int                  rc;
	struct m0_conf_obj  *obj = NULL;
	struct m0_conf_objv *objv;
	struct m0_conf_obj  *obj_parent;
	struct m0_conf_objv *objv_parent;
	struct m0_conf_obj  *real_obj;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_OBJV_TYPE, &obj },
			      {parent, &M0_CONF_OBJV_TYPE, &obj_parent},
			      {real, &M0_CONF_CONTROLLER_TYPE, &real_obj});
	if (rc != 0)
		goto fail;

	objv = M0_CONF_CAST(obj, m0_conf_objv);
	objv->cv_real = real_obj;
	rc = spiel_controllerv_dirs_create(&tx->spt_cache, objv);
	if (rc != 0)
		goto fail;
	objv_parent = M0_CONF_CAST(obj_parent, m0_conf_objv);
	if (objv_parent->cv_children == NULL) {
		/* Parent dir does not exist ==> create it. */
		rc = spiel_enclosurev_dirs_create(&tx->spt_cache, objv_parent);
		if (rc != 0)
			goto fail;
	}
	m0_conf_dir_add(objv_parent->cv_children, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_controller_v_add);

int m0_spiel_disk_v_add(struct m0_spiel_tx  *tx,
			const struct m0_fid *fid,
			const struct m0_fid *parent,
			const struct m0_fid *real)
{
	int                  rc;
	struct m0_conf_obj  *obj = NULL;
	struct m0_conf_objv *objv;
	struct m0_conf_obj  *obj_parent;
	struct m0_conf_objv *objv_parent;
	struct m0_conf_obj  *real_obj;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_OBJV_TYPE, &obj },
			      {parent, &M0_CONF_OBJV_TYPE, &obj_parent},
			      {real, &M0_CONF_DISK_TYPE, &real_obj});
	if (rc != 0)
		goto fail;

	objv = M0_CONF_CAST(obj, m0_conf_objv);
	objv->cv_real = real_obj;

	objv_parent = M0_CONF_CAST(obj_parent, m0_conf_objv);
	if (objv_parent->cv_children == NULL) {
		/* Parent dir does not exist ==> create it. */
		rc = spiel_controllerv_dirs_create(&tx->spt_cache, objv_parent);
		if (rc != 0)
			goto fail;
	}
	m0_conf_dir_add(objv_parent->cv_children, obj);
	obj->co_status = M0_CS_READY;

	M0_POST(m0_conf_obj_invariant(obj));
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(0);
fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_disk_v_add);

static int spiel_pver_add(struct m0_conf_obj **obj_v, struct m0_conf_pver *pver)
{
	struct m0_conf_obj             *obj;
	const struct m0_conf_obj_type  *obj_type;
	struct m0_conf_pver           **pvers;
	struct m0_conf_pver           **pvers_new;
	unsigned                        nr_pvers;

	M0_ENTRY();
	/* TODO: There m0_conf_pver::pv_nr_failures should be checked and Mero
	 * has a function named m0_fd_tolerance_check() for it. But the function
	 * cannot be called here, because it uses m0_confc for walking on the
	 * configuration tree and tx->spt_cache doesn't belong to any confc
	 * instance. Maybe m0_fd_tolerance_check() should be rewritten to not
	 * use confc and work with the m0_conf_cache only.
	 */

	obj = M0_CONF_CAST(*obj_v, m0_conf_objv)->cv_real;
	obj_type = m0_conf_obj_type(obj);
	pvers = m0_conf_pvers(obj);
	for (nr_pvers = 0; pvers != NULL && pvers[nr_pvers] != NULL; ++nr_pvers)
		/* count the elements */;

	/* Element count = old count + new + finish NULL */
	M0_ALLOC_ARR(pvers_new, nr_pvers + 2);
	if (pvers_new == NULL || M0_FI_ENABLED("fail_allocation"))
		return M0_ERR(-ENOMEM);
	memcpy(pvers_new, pvers, nr_pvers * sizeof(*pvers));
	pvers_new[nr_pvers] = pver;

	if (obj_type == &M0_CONF_RACK_TYPE)
		M0_CONF_CAST(obj, m0_conf_rack)->cr_pvers = pvers_new;
	else if (obj_type == &M0_CONF_ENCLOSURE_TYPE)
		M0_CONF_CAST(obj, m0_conf_enclosure)->ce_pvers = pvers_new;
	else if (obj_type == &M0_CONF_CONTROLLER_TYPE)
		M0_CONF_CAST(obj, m0_conf_controller)->cc_pvers = pvers_new;
	else if (obj_type == &M0_CONF_DISK_TYPE)
		M0_CONF_CAST(obj, m0_conf_disk)->ck_pvers = pvers_new;
	else
		M0_IMPOSSIBLE("");

	m0_free(pvers);
	return M0_RC(0);
}

/**
 * Removes `pver' element from m0_conf_pvers(obj).
 *
 * @note spiel_pver_delete() does not change the size of pvers array.
 * If pvers has pver then after remove it pvers has two NULLs end.
 */
static int spiel_pver_delete(struct m0_conf_obj        *obj,
			     const struct m0_conf_pver *pver)
{
	struct m0_conf_pver **pvers = m0_conf_pvers(obj);
	unsigned              i;
	bool                  found = false;

	M0_ENTRY();
	M0_PRE(pver != NULL);

	if (pvers == NULL)
		return M0_ERR(-ENOENT);

	for (i = 0; pvers[i] != NULL; ++i) {
		if (pvers[i] == pver)
			found = true;
		if (found)
			pvers[i] = pvers[i + 1];
	}
	return M0_RC(found ? 0 : -ENOENT);
}

static int spiel_objv_remove(struct m0_conf_obj  **obj,
			     struct m0_conf_pver  *pver)
{
	struct m0_conf_objv *objv = M0_CONF_CAST(*obj, m0_conf_objv);

	if (objv->cv_children != NULL)
		m0_conf_cache_del((*obj)->co_cache, &objv->cv_children->cd_obj);
	m0_conf_obj_put(*obj);
	m0_conf_dir_tlist_del(*obj);
	m0_conf_cache_del((*obj)->co_cache, *obj);
	*obj = NULL;
	return M0_RC(0);
}

static int spiel_pver_iterator(struct m0_conf_obj  *dir,
			       struct m0_conf_pver *pver,
			       int (*action)(struct m0_conf_obj**,
					     struct m0_conf_pver*))
{
	int                  rc;
	struct m0_conf_obj  *entry;
	struct m0_conf_objv *objv;

	m0_conf_obj_get(dir); /* required by ->coo_readdir() */
	for (entry = NULL; (rc = dir->co_ops->coo_readdir(dir, &entry)) > 0; ) {
		/* All configuration is expected to be available. */
		M0_ASSERT(rc != M0_CONF_DIRMISS);

		objv = M0_CONF_CAST(entry, m0_conf_objv);
		rc = objv->cv_children == NULL ? 0 :
		      spiel_pver_iterator(&objv->cv_children->cd_obj, pver,
					  action);
		rc = rc ?: action(&entry, pver);
		if (rc != 0) {
			m0_conf_obj_put(entry);
			break;
		}
	}
	m0_conf_obj_put(dir);
	return M0_RC(rc);
}

int m0_spiel_pool_version_done(struct m0_spiel_tx  *tx,
			       const struct m0_fid *fid)
{
	int                  rc;
	struct m0_conf_pver *pver;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);
	pver = M0_CONF_CAST(m0_conf_cache_lookup(&tx->spt_cache, fid),
			    m0_conf_pver);
	M0_ASSERT(pver != NULL);
	rc = spiel_pver_iterator(&pver->pv_rackvs->cd_obj, pver,
				 &spiel_pver_add);
	if (rc != 0) {
		spiel_pver_iterator(&pver->pv_rackvs->cd_obj, pver,
				    &spiel_objv_remove);
		/*
		 * TODO: Remove this line once m0_spiel_element_del removes
		 * the object itself and all its m0_conf_dir members.
		 */
		m0_conf_cache_del(&tx->spt_cache, &pver->pv_rackvs->cd_obj);

		m0_mutex_unlock(&tx->spt_lock);
		m0_spiel_element_del(tx, fid);
	} else {
		m0_mutex_unlock(&tx->spt_lock);
	}
	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_pool_version_done);

static void spiel_pver_remove(struct m0_conf_cache *cache,
			      struct m0_conf_pver  *pver)
{
	struct m0_conf_obj            *obj;
	const struct m0_conf_obj_type *ot;

	M0_ENTRY();

	m0_tl_for(m0_conf_cache, &cache->ca_registry, obj) {
		ot = m0_conf_obj_type(obj);
		if (M0_IN(ot, (&M0_CONF_RACK_TYPE, &M0_CONF_ENCLOSURE_TYPE,
			       &M0_CONF_CONTROLLER_TYPE, &M0_CONF_DISK_TYPE)))
			spiel_pver_delete(obj, pver);
	} m0_tl_endfor;

	M0_LEAVE();
}

int m0_spiel_element_del(struct m0_spiel_tx *tx, const struct m0_fid *fid)
{
	int                 rc = 0;
	struct m0_conf_obj *obj;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);
	obj = m0_conf_cache_lookup(&tx->spt_cache, fid);
	if (obj != NULL) {
		if (m0_conf_obj_type(obj) == &M0_CONF_PVER_TYPE)
			spiel_pver_remove(&tx->spt_cache,
					  M0_CONF_CAST(obj, m0_conf_pver));
		m0_conf_dir_tlist_del(obj);
		m0_conf_cache_del(&tx->spt_cache, obj);
	}
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_element_del);

static int spiel_str_to_file(char *str, const char *filename)
{
#ifdef __KERNEL__
	return 0;
#else
	int   rc;
	FILE *file;

	file = fopen(filename, "w+");
	if (file == NULL)
		return errno;
	rc = fwrite(str, strlen(str), 1, file)==1 ? 0 : -EINVAL;
	fclose(file);
	return rc;
#endif
}

static int spiel_tx_to_str(struct m0_spiel_tx *tx,
			   uint64_t            ver_forced,
			   char              **str,
			   bool                debug)
{
	M0_ENTRY();
	M0_PRE(ver_forced != M0_CONF_VER_UNKNOWN);

	return M0_RC(spiel_root_ver_update(tx, ver_forced) ?:
		     m0_conf_cache_to_string(&tx->spt_cache, str, debug));
}

int m0_spiel_tx_to_str(struct m0_spiel_tx *tx,
		       uint64_t            ver_forced,
		       char              **str)
{
	return spiel_tx_to_str(tx, ver_forced, str, false);
}
M0_EXPORTED(m0_spiel_tx_to_str);

void m0_spiel_tx_str_free(char *str)
{
	m0_confx_string_free(str);
}
M0_EXPORTED(m0_spiel_tx_str_free);

static int spiel_tx_dump(struct m0_spiel_tx *tx,
			 uint64_t            ver_forced,
			 const char         *filename,
			 bool                debug)
{
	int   rc;
	char *buffer;

	M0_ENTRY();
	rc = spiel_tx_to_str(tx, ver_forced, &buffer, debug) ?:
		spiel_str_to_file(buffer, filename);
	m0_spiel_tx_str_free(buffer);
	return M0_RC(rc);
}

int m0_spiel_tx_dump(struct m0_spiel_tx *tx, uint64_t ver_forced,
		     const char *filename)
{
	return M0_RC(spiel_tx_dump(tx, ver_forced, filename, false));
}
M0_EXPORTED(m0_spiel_tx_dump);

int m0_spiel_tx_dump_debug(struct m0_spiel_tx *tx, uint64_t ver_forced,
			   const char *filename)
{
	return M0_RC(spiel_tx_dump(tx, ver_forced, filename, true));
}
M0_EXPORTED(m0_spiel_tx_dump_debug);

/** @} */
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
