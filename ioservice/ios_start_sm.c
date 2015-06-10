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
 * Original author: Mikhail Antropov <mikhail.antropov@xyratex.com>
 * Original creation date: 26-Aug-2015
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_IOSERVICE
#include "lib/trace.h"

#include "lib/types.h"
#include "lib/errno.h"
#include "lib/lockers.h"
#include "lib/memory.h"              /* M0_ALLOC_PTR */
#include "lib/misc.h"                /* M0_SET0 */
#include "be/seg.h"
#include "be/tx.h"
#include "cob/cob.h"
#include "conf/obj_ops.h"
#include "ioservice/io_service.h"
#include "ioservice/ios_start_sm.h"
#include "mero/setup.h"
#include "module/instance.h"         /* m0_get() */
#include "pool/pool.h"
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"

/**
 * Please See ioservice/io_service.c
 * The key is alloted in m0_ios_register().
 */
M0_EXTERN unsigned poolmach_key;

M0_INTERNAL int m0_ios_create_buffer_pool(struct m0_reqh_service *service);
M0_INTERNAL void m0_ios_delete_buffer_pool(struct m0_reqh_service *service);

struct ios_start_sm_hadler {
	/** AST callback */
	void (*iosh_ast_cb)(struct m0_sm_group *, struct m0_sm_ast *);
};

static struct m0_sm_state_descr ios_start_sm_states[] = {
	[M0_IOS_START_INIT] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "ios_start_init",
		.sd_allowed = M0_BITS(M0_IOS_START_CDOM_CREATE,
				      M0_IOS_START_PM_INIT,
				      M0_IOS_START_FAILURE)
	},
	[M0_IOS_START_CDOM_CREATE] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_be_create",
		.sd_allowed = M0_BITS(M0_IOS_START_CDOM_CREATE_RES)
	},
	[M0_IOS_START_CDOM_CREATE_RES] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_be_result",
		.sd_allowed = M0_BITS(M0_IOS_START_MKFS, M0_IOS_START_PM_INIT,
				      M0_IOS_START_FAILURE)
	},
	[M0_IOS_START_MKFS] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_mkfs",
		.sd_allowed = M0_BITS(M0_IOS_START_MKFS_RESULT)
	},
	[M0_IOS_START_MKFS_RESULT] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_mkfs_result",
		.sd_allowed = M0_BITS(M0_IOS_START_PM_INIT,
				      M0_IOS_START_FAILURE)
	},
	[M0_IOS_START_PM_INIT] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_pm_init",
		.sd_allowed = M0_BITS(M0_IOS_START_CONF_FS_GET)
	},
	[M0_IOS_START_CONF_FS_GET] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_fs_get",
		.sd_allowed = M0_BITS(M0_IOS_START_CONF_ITER_INIT,
				      M0_IOS_START_FAILURE)
	},
	[M0_IOS_START_CONF_ITER_INIT] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_conf_iter_init",
		.sd_allowed = M0_BITS(M0_IOS_START_CONF_ITER,
				      M0_IOS_START_FAILURE)
	},
	[M0_IOS_START_CONF_ITER] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_conf_iter",
		.sd_allowed = M0_BITS(M0_IOS_START_FINAL, M0_IOS_START_FAILURE)
	},
	[M0_IOS_START_FAILURE] = {
		.sd_flags     = M0_SDF_FAILURE | M0_SDF_FINAL,
		.sd_name      = "ios_start_failure",
	},
	[M0_IOS_START_FINAL] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "ios_start_fini",
		.sd_allowed = 0
	},
};

static const struct m0_sm_conf ios_start_sm_conf = {
	.scf_name      = "ios_cdom conf",
	.scf_nr_states = ARRAY_SIZE(ios_start_sm_states),
	.scf_state     = ios_start_sm_states
};

static void ios_start_sm_tick(struct m0_ios_start_sm *ios_sm);

static struct m0_ios_start_sm *ios_start_clink2sm(struct m0_clink *cl)
{
	return container_of(cl, struct m0_ios_start_sm, ism_clink);
}

static struct m0_ios_start_sm *ios_start_ast2sm(struct m0_sm_ast *ast)
{
	return (struct m0_ios_start_sm *)ast->sa_datum;
}

M0_INTERNAL void m0_ios_start_lock(struct m0_ios_start_sm *ios_sm)
{
	m0_sm_group_lock(ios_sm->ism_sm.sm_grp);
}

M0_INTERNAL void m0_ios_start_unlock(struct m0_ios_start_sm *ios_sm)
{
	m0_sm_group_unlock(ios_sm->ism_sm.sm_grp);
}

static bool ios_start_is_locked(const struct m0_ios_start_sm *ios_sm)
{
	return m0_mutex_is_locked(&ios_sm->ism_sm.sm_grp->s_lock);
}

static enum m0_ios_start_state ios_start_state_get(
					const struct m0_ios_start_sm *ios_sm)
{
	return (enum m0_ios_start_state)ios_sm->ism_sm.sm_state;
}

static void ios_start_state_set(struct m0_ios_start_sm  *ios_sm,
				enum m0_ios_start_state  state)
{
	M0_PRE(ios_start_is_locked(ios_sm));

	M0_LOG(M0_DEBUG, "IO start sm:%p, state_change:[%s -> %s]", ios_sm,
		m0_sm_state_name(&ios_sm->ism_sm, ios_start_state_get(ios_sm)),
		m0_sm_state_name(&ios_sm->ism_sm, state));
	m0_sm_state_set(&ios_sm->ism_sm, state);
}

static void ios_start_sm_failure(struct m0_ios_start_sm *ios_sm, int rc)
{
	enum m0_ios_start_state state = ios_start_state_get(ios_sm);

	switch (state) {
	case M0_IOS_START_CDOM_CREATE_RES:
		/* Possibly errors source: tx close */
		if (ios_sm->ism_dom != NULL)
			m0_cob_domain_fini(ios_sm->ism_dom);
		break;
	case M0_IOS_START_MKFS:
		/* Possibly errors source: tx open */
		m0_cob_domain_destroy(ios_sm->ism_dom, ios_sm->ism_sm.sm_grp);
		m0_cob_domain_fini(ios_sm->ism_dom);
		break;
	case M0_IOS_START_MKFS_RESULT:
		/* Possibly errors source: tx close, m0_cob_domain_mkfs,
					   ios_start_pm_prepare */
		m0_cob_domain_destroy(ios_sm->ism_dom, ios_sm->ism_sm.sm_grp);
		m0_cob_domain_fini(ios_sm->ism_dom);
		m0_ios_delete_buffer_pool(ios_sm->ism_service);
		break;
	case M0_IOS_START_PM_INIT:
		/* Possibly errors source: tx open */
		/* fallthrough */
	case M0_IOS_START_CONF_FS_GET:
		/* Possibly errors source: tx close, m0_poolmach_backed_init,
					   m0_confc_ctx_init */
		/* fallthrough */
	case M0_IOS_START_CONF_ITER_INIT:
		/* Possibly errors source: m0_confc_...  */
		/* fallthrough */
	case M0_IOS_START_FINAL:
		/* Possibly errors source: tx close */
		m0_ios_delete_buffer_pool(ios_sm->ism_service);
		m0_cob_domain_fini(ios_sm->ism_dom);
		m0_free(ios_sm->ism_poolmach);
		break;
	default:
		M0_ASSERT(false);
		break;
	}

	m0_sm_fail(&ios_sm->ism_sm, M0_IOS_START_FAILURE, rc);
}

M0_INTERNAL int m0_ios_start_sm_init(struct m0_ios_start_sm  *ios_sm,
				     struct m0_reqh_service  *service,
				     struct m0_sm_group      *grp)
{
	static uint64_t cid = M0_IOS_COB_ID_START;

	M0_ENTRY();
	M0_PRE(ios_sm != NULL);
	M0_PRE(service != NULL);

	ios_sm->ism_cdom_id.id = cid++;
	ios_sm->ism_dom = NULL;
	ios_sm->ism_service = service;
	ios_sm->ism_reqh = service->rs_reqh;
	ios_sm->ism_last_rc = 0;

	m0_sm_init(&ios_sm->ism_sm, &ios_start_sm_conf, M0_IOS_START_INIT, grp);

	return M0_RC(0);
}

M0_INTERNAL void m0_ios_start_sm_exec(struct m0_ios_start_sm *ios_sm)
{
	M0_ENTRY();
	ios_start_sm_tick(ios_sm);
	M0_LEAVE();
}

static void ios_start_tx_waiter(struct m0_clink *cl, uint32_t flag)
{
	struct m0_ios_start_sm *ios_sm = ios_start_clink2sm(cl);
	struct m0_sm           *tx_sm = &ios_sm->ism_tx.t_sm;

	if (M0_IN(tx_sm->sm_state, (flag, M0_BTS_FAILED))) {
		if (tx_sm->sm_rc != 0)
			ios_start_sm_failure(ios_sm, tx_sm->sm_rc);
		else
			ios_start_sm_tick(ios_sm);
		m0_clink_del(cl);
		m0_clink_fini(cl);
	}
}

static bool ios_start_tx_open_wait_cb(struct m0_clink *cl)
{
	ios_start_tx_waiter(cl, M0_BTS_ACTIVE);
	return true;
}

static bool ios_start_tx_done_wait_cb(struct m0_clink *cl)
{
	ios_start_tx_waiter(cl, M0_BTS_DONE);
	return true;
}

static void ios_start_tx_open(struct m0_ios_start_sm *ios_sm, bool exclusive)
{
	struct m0_sm *tx_sm = &ios_sm->ism_tx.t_sm;

	m0_clink_init(&ios_sm->ism_clink, ios_start_tx_open_wait_cb);
	m0_clink_add(&tx_sm->sm_chan, &ios_sm->ism_clink);
	if (exclusive)
		m0_be_tx_exclusive_open(&ios_sm->ism_tx);
	else
		m0_be_tx_open(&ios_sm->ism_tx);
}

static void ios_start_tx_close(struct m0_ios_start_sm *ios_sm)
{
	struct m0_sm *tx_sm = &ios_sm->ism_tx.t_sm;

	m0_clink_init(&ios_sm->ism_clink, ios_start_tx_done_wait_cb);
	m0_clink_add(&tx_sm->sm_chan, &ios_sm->ism_clink);
	m0_be_tx_close(&ios_sm->ism_tx);
}

static int ios_start_pm_prepare(struct m0_ios_start_sm *ios_sm)
{
	struct m0_be_tx_credit  cred  = {};
	struct m0_be_seg       *seg = ios_sm->ism_reqh->rh_beseg;
	struct m0_be_tx        *tx = &ios_sm->ism_tx;
	struct m0_mero         *mero;
	int                     rc;

	rc = m0_ios_create_buffer_pool(ios_sm->ism_service);
	if (rc != 0) {
		return M0_ERR(rc);
	}

	M0_ALLOC_PTR(ios_sm->ism_poolmach);
	if (ios_sm->ism_poolmach == NULL) {
		m0_ios_delete_buffer_pool(ios_sm->ism_service);
		return M0_ERR(-ENOMEM);
	}

	mero = ios_sm->ism_service->rs_reqh_ctx->rc_mero;
	m0_poolmach_store_init_creds_add(seg, PM_DEFAULT_NR_NODES,
					 mero->cc_pool_width,
					 mero->cc_pool_width, &cred);
	m0_be_tx_init(tx, 0, seg->bs_domain, ios_sm->ism_sm.sm_grp,
		      NULL, NULL, NULL, NULL);
	m0_be_tx_prep(tx, &cred);

	ios_start_state_set(ios_sm, M0_IOS_START_PM_INIT);
	ios_start_tx_open(ios_sm, true);
	return M0_RC(0);
}

static void ios_start_cdom_tx_open(struct m0_ios_start_sm *ios_sm)
{
	struct m0_be_tx_credit  cred  = {};
	struct m0_be_seg       *seg = ios_sm->ism_reqh->rh_beseg;
	struct m0_be_tx        *tx = &ios_sm->ism_tx;

	m0_cob_domain_credit_add(ios_sm->ism_dom, tx, seg,
				 &ios_sm->ism_cdom_id, &cred);

	m0_be_tx_init(tx, 0, seg->bs_domain, ios_sm->ism_sm.sm_grp,
		      NULL, NULL, NULL, NULL);
	m0_be_tx_prep(tx, &cred);

	ios_start_state_set(ios_sm, M0_IOS_START_CDOM_CREATE);
	ios_start_tx_open(ios_sm, true);
}

static void ios_start_ast_start(struct m0_sm_group *grp,
				struct m0_sm_ast   *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);
	int                     rc;

	M0_ENTRY();
	M0_ASSERT(ios_start_state_get(ios_sm) == M0_IOS_START_INIT);
	m0_rwlock_read_lock(&ios_sm->ism_reqh->rh_rwlock);
	ios_sm->ism_dom = m0_reqh_lockers_get(ios_sm->ism_reqh,
					       m0_get()->i_ios_cdom_key);
	m0_rwlock_read_unlock(&ios_sm->ism_reqh->rh_rwlock);
	if (ios_sm->ism_dom == NULL) {
		ios_start_cdom_tx_open(ios_sm);
	} else {
		rc = m0_cob_domain_init(ios_sm->ism_dom,
					ios_sm->ism_reqh->rh_beseg,
					NULL);
		if (rc == 0) {
			rc = ios_start_pm_prepare(ios_sm);
			if (rc != 0)
				m0_cob_domain_fini(ios_sm->ism_dom);
		}
		if (rc != 0)
			ios_start_sm_failure(ios_sm, rc);
	}
	M0_LEAVE();
}

static void ios_start_ast_cdom_create(struct m0_sm_group *grp,
				   struct m0_sm_ast *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);
	int                    rc;

	M0_ASSERT(ios_start_state_get(ios_sm) == M0_IOS_START_CDOM_CREATE);

	rc = m0_cob_domain_create_prepared(&ios_sm->ism_dom,
					   grp,
					   &ios_sm->ism_cdom_id,
					   ios_sm->ism_reqh->rh_beseg,
					   &ios_sm->ism_tx);

	if (rc != 0)
		ios_sm->ism_last_rc = M0_ERR(rc);

	ios_start_state_set(ios_sm, M0_IOS_START_CDOM_CREATE_RES);
	ios_start_tx_close(ios_sm);
}

static void ios_start_cob_tx_open(struct m0_ios_start_sm *ios_sm)
{
	struct m0_be_tx_credit  cred  = {};
	struct m0_be_seg       *seg = ios_sm->ism_reqh->rh_beseg;

	M0_LOG(M0_DEBUG, "key init for reqh=%p, key=%d",
	       ios_sm->ism_reqh, m0_get()->i_ios_cdom_key);

	m0_cob_tx_credit(ios_sm->ism_dom, M0_COB_OP_DOMAIN_MKFS, &cred);
	m0_be_tx_init(&ios_sm->ism_tx, 0, seg->bs_domain,
		      ios_sm->ism_sm.sm_grp, NULL, NULL, NULL, NULL);
	m0_be_tx_prep(&ios_sm->ism_tx, &cred);

	ios_start_tx_open(ios_sm, false);
}

static void ios_start_ast_be_result(struct m0_sm_group *grp,
				    struct m0_sm_ast   *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);

	M0_ASSERT(ios_start_state_get(ios_sm) == M0_IOS_START_CDOM_CREATE_RES);

	m0_be_tx_fini(&ios_sm->ism_tx);

	if (ios_sm->ism_last_rc != 0) {
		ios_start_sm_failure(ios_sm, ios_sm->ism_last_rc);
	} else {
		/*
		 * COB domain is successfully created.
		 * Store its address in m0 instance and then format it.
		 */
		m0_rwlock_write_lock(&ios_sm->ism_reqh->rh_rwlock);
		m0_reqh_lockers_set(ios_sm->ism_reqh, m0_get()->i_ios_cdom_key,
				    ios_sm->ism_dom);
		m0_rwlock_write_unlock(&ios_sm->ism_reqh->rh_rwlock);

		ios_start_state_set(ios_sm, M0_IOS_START_MKFS);
		ios_start_cob_tx_open(ios_sm);
	}
}

static void ios_start_ast_mkfs(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);

	M0_ASSERT(ios_start_state_get(ios_sm) == M0_IOS_START_MKFS);

	ios_sm->ism_last_rc = m0_cob_domain_mkfs(ios_sm->ism_dom,
						 &M0_MDSERVICE_SLASH_FID,
						 &ios_sm->ism_tx);

	ios_start_state_set(ios_sm, M0_IOS_START_MKFS_RESULT);
	ios_start_tx_close(ios_sm);
}

static void ios_start_ast_mkfs_result(struct m0_sm_group *grp,
				     struct m0_sm_ast *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);
	int                     rc;

	M0_ASSERT(ios_start_state_get(ios_sm) == M0_IOS_START_MKFS_RESULT);

	m0_be_tx_fini(&ios_sm->ism_tx);

	rc = ios_sm->ism_last_rc ?: ios_start_pm_prepare(ios_sm);
	if (rc != 0)
		ios_start_sm_failure(ios_sm, ios_sm->ism_last_rc);
}

static void ios_start_ast_pm_init(struct m0_sm_group *grp,
				  struct m0_sm_ast   *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);
	struct m0_mero         *mero;
	int                     rc;

	M0_ASSERT(ios_start_state_get(ios_sm) == M0_IOS_START_PM_INIT);

	mero = ios_sm->ism_service->rs_reqh_ctx->rc_mero;
	rc = m0_poolmach_backed_init(ios_sm->ism_poolmach,
				     ios_sm->ism_reqh->rh_beseg,
				     &ios_sm->ism_tx,
				     PM_DEFAULT_NR_NODES,
				     mero->cc_pool_width,
				     PM_DEFAULT_MAX_NODE_FAILURES,
				     mero->cc_pool_width);
	if (rc != 0)
		ios_sm->ism_last_rc = M0_ERR(rc);

	ios_start_state_set(ios_sm, M0_IOS_START_CONF_FS_GET);
	ios_start_tx_close(ios_sm);
}

static bool ios_start_ast_conf_fs_get_cb(struct m0_clink *cl)
{
	struct m0_ios_start_sm *ios_sm = ios_start_clink2sm(cl);
	int                    rc;

	if (m0_confc_ctx_is_completed(&ios_sm->ism_confc_ctx)) {
		rc = m0_confc_ctx_error(&ios_sm->ism_confc_ctx);
		if (rc != 0)
			ios_start_sm_failure(ios_sm, rc);
		else
			ios_start_sm_tick(ios_sm);

		m0_clink_del(cl);
		m0_clink_fini(cl);
	}

	return true;
}

static int ios_start_fs_obj_open(struct m0_ios_start_sm *ios_sm)
{
	struct m0_confc *confc = &ios_sm->ism_reqh->rh_confc;

	M0_ENTRY();
	m0_confc_ctx_init(&ios_sm->ism_confc_ctx, confc);
	if (!ios_sm->ism_confc_ctx.fc_allowed) {
		m0_confc_ctx_fini(&ios_sm->ism_confc_ctx);
		return M0_ERR(-ENOENT);
	}

	m0_clink_init(&ios_sm->ism_clink, ios_start_ast_conf_fs_get_cb);
	m0_clink_add(&ios_sm->ism_confc_ctx.fc_mach.sm_chan,
		     &ios_sm->ism_clink);

	m0_confc_open(&ios_sm->ism_confc_ctx, confc->cc_root,
			M0_CONF_ROOT_PROFILES_FID,
			ios_sm->ism_reqh->rh_profile,
			M0_CONF_PROFILE_FILESYSTEM_FID);
	return M0_RC(0);
}

static void ios_start_fs_obj_close(struct m0_ios_start_sm *ios_sm)
{
	M0_ENTRY();
	m0_confc_close(ios_sm->ism_fs_obj);
	m0_confc_ctx_fini_locked(&ios_sm->ism_confc_ctx);
	M0_LEAVE();
}

static void ios_start_ast_conf_fs_get(struct m0_sm_group *grp,
				     struct m0_sm_ast *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);
	int                     rc;

	M0_ASSERT(ios_start_state_get(ios_sm) == M0_IOS_START_CONF_FS_GET);

	m0_be_tx_fini(&ios_sm->ism_tx);

	rc = ios_sm->ism_last_rc ?: ios_start_fs_obj_open(ios_sm);
	if (rc == 0)
		ios_start_state_set(ios_sm, M0_IOS_START_CONF_ITER_INIT);
	else
		ios_start_sm_failure(ios_sm, rc);
}

static void ios_start_pm_set(struct m0_ios_start_sm *ios_sm)
{
	m0_rwlock_write_lock(&ios_sm->ism_reqh->rh_rwlock);
	m0_reqh_lockers_set(ios_sm->ism_reqh, poolmach_key,
			    ios_sm->ism_poolmach);
	m0_rwlock_write_unlock(&ios_sm->ism_reqh->rh_rwlock);
}

static void ios_start_pm_disk_add(struct m0_ios_start_sm *ios_sm,
				  struct m0_conf_obj     *obj)
{
	struct m0_conf_disk *d;
	int                  idx;

	M0_ASSERT(ios_start_state_get(ios_sm) == M0_IOS_START_CONF_ITER);
	M0_ASSERT(obj != NULL && m0_conf_obj_type(obj) == &M0_CONF_DISK_TYPE);
	d = M0_CONF_CAST(obj, m0_conf_disk);
	idx = ios_sm->ism_pool_index++;
	ios_sm->ism_poolmach->pm_state->
		pst_devices_array[idx].pd_id = d->ck_obj.co_id;
}

static bool _obj_is_disk(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_DISK_TYPE;
}

static void ios_start_conf_disk_next(struct m0_ios_start_sm *ios_sm)
{
	struct m0_mutex *sm_grp_lock = &ios_sm->ism_sm.sm_grp->s_lock;
	int              rc;

	/**
	 * @todo m0_conf_diter_next take sm_grp_lock internally and there is no
	 * function for users who already have a lock.
	 * Corresponding ticket: MERO-1189.
	 */
	m0_mutex_unlock(sm_grp_lock);
	while ((rc = m0_conf_diter_next(&ios_sm->ism_it, _obj_is_disk)) ==
			M0_CONF_DIRNEXT) {
		m0_mutex_lock(sm_grp_lock);
		ios_start_pm_disk_add(ios_sm,
				      m0_conf_diter_result(&ios_sm->ism_it));
		m0_mutex_unlock(sm_grp_lock);
	}
	m0_mutex_lock(sm_grp_lock);

	/*
	 * If rc == M0_CONF_DIRMISS, then ios_start_disk_iter_cb() will be
	 * called once directory entry is loaded
	 */
	if (rc != M0_CONF_DIRMISS) {
		/* End of directory or error */
		m0_clink_del_lock(&ios_sm->ism_clink);
		m0_clink_fini(&ios_sm->ism_clink);
		m0_mutex_unlock(sm_grp_lock);
		m0_conf_diter_fini(&ios_sm->ism_it);
		m0_mutex_lock(sm_grp_lock);
		ios_start_fs_obj_close(ios_sm);

		if (rc == M0_CONF_DIREND) {
			ios_start_pm_set(ios_sm);
			ios_start_state_set(ios_sm, M0_IOS_START_FINAL);
		} else {
			ios_start_sm_failure(ios_sm, rc);
		}
	}
}

void ios_start_conf_iter_ast(struct m0_sm_group *grp,
			     struct m0_sm_ast   *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);

	ios_start_conf_disk_next(ios_sm);
}

static bool ios_start_disk_iter_cb(struct m0_clink *cl)
{
	struct m0_ios_start_sm *ios_sm = ios_start_clink2sm(cl);

	ios_start_sm_tick(ios_sm);
	return true;
}

static void ios_start_ast_conf_iter_init(struct m0_sm_group *grp,
					 struct m0_sm_ast   *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);
	struct m0_confc        *confc;
	int                     rc;

	M0_ASSERT(ios_start_state_get(ios_sm) == M0_IOS_START_CONF_ITER_INIT);

	ios_sm->ism_fs_obj = m0_confc_ctx_result(&ios_sm->ism_confc_ctx);

	confc = &ios_sm->ism_reqh->rh_confc;
	rc = m0_conf_diter_init(&ios_sm->ism_it, confc, ios_sm->ism_fs_obj,
				M0_CONF_FILESYSTEM_RACKS_FID,
				M0_CONF_RACK_ENCLS_FID,
				M0_CONF_ENCLOSURE_CTRLS_FID,
				M0_CONF_CONTROLLER_DISKS_FID);
	if (rc != 0) {
		ios_start_fs_obj_close(ios_sm);
		ios_start_sm_failure(ios_sm, rc);
		return;
	}

	ios_sm->ism_pool_index = 0;

	ios_start_state_set(ios_sm, M0_IOS_START_CONF_ITER);

	m0_clink_init(&ios_sm->ism_clink, ios_start_disk_iter_cb);
	m0_clink_add_lock(&ios_sm->ism_it.di_wait, &ios_sm->ism_clink);
	ios_start_conf_disk_next(ios_sm);
}

static void ios_start_sm_tick(struct m0_ios_start_sm *ios_sm)
{
	int                             state;
	const struct ios_start_sm_hadler handlers[] = {
		[M0_IOS_START_INIT] = {ios_start_ast_start},
		[M0_IOS_START_CDOM_CREATE] = {ios_start_ast_cdom_create},
		[M0_IOS_START_CDOM_CREATE_RES] = {ios_start_ast_be_result},
		[M0_IOS_START_MKFS] = {ios_start_ast_mkfs},
		[M0_IOS_START_MKFS_RESULT] = {ios_start_ast_mkfs_result},
		[M0_IOS_START_PM_INIT] = {ios_start_ast_pm_init},
		[M0_IOS_START_CONF_FS_GET] = {ios_start_ast_conf_fs_get},
		[M0_IOS_START_CONF_ITER_INIT] = {ios_start_ast_conf_iter_init},
		[M0_IOS_START_CONF_ITER] = {ios_start_conf_iter_ast}
	};

	state = ios_start_state_get(ios_sm);
	M0_ASSERT(handlers[state].iosh_ast_cb != NULL);
	M0_SET0(&ios_sm->ism_ast);
	ios_sm->ism_ast.sa_cb = handlers[state].iosh_ast_cb;
	ios_sm->ism_ast.sa_datum = ios_sm;
	m0_sm_ast_post(ios_sm->ism_sm.sm_grp, &ios_sm->ism_ast);
}

M0_INTERNAL void m0_ios_start_sm_fini(struct m0_ios_start_sm *ios_sm)
{
	M0_ENTRY("ios_sm: %p", ios_sm);
	M0_PRE(ios_sm != NULL);
	M0_PRE(M0_IN(ios_start_state_get(ios_sm),
		(M0_IOS_START_FINAL, M0_IOS_START_FAILURE)));
	M0_PRE(ios_start_is_locked(ios_sm));

	m0_sm_fini(&ios_sm->ism_sm);
	M0_LEAVE();
}

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
