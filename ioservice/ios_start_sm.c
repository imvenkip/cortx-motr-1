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
#include "fid/fid_list.h"            /* m0_fids_tlist_XXX */
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

static struct m0_sm_state_descr ios_start_sm_states[] = {
	[M0_IOS_START_INIT] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "ios_start_init",
		.sd_allowed = M0_BITS(M0_IOS_START_CDOM_CREATE,
				      M0_IOS_START_BUFFER_POOL_CREATE,
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
		.sd_allowed = M0_BITS(M0_IOS_START_MKFS,
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
		.sd_allowed = M0_BITS(M0_IOS_START_BUFFER_POOL_CREATE,
				      M0_IOS_START_FAILURE)
	},
	[M0_IOS_START_BUFFER_POOL_CREATE] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_buffer_pool_create",
		.sd_allowed = M0_BITS(M0_IOS_START_CONF_COUNTER_INIT,
				      M0_IOS_START_FAILURE)
	},
	[M0_IOS_START_CONF_COUNTER_INIT] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_conf_counter_init",
		.sd_allowed = M0_BITS(M0_IOS_START_CONF_COUNTER_NODES,
				      M0_IOS_START_FAILURE)
	},
	[M0_IOS_START_CONF_COUNTER_NODES] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_conf_counter_nodes",
		.sd_allowed = M0_BITS(M0_IOS_START_CONF_COUNTER_SDEVS,
				      M0_IOS_START_FAILURE)
	},
	[M0_IOS_START_CONF_COUNTER_SDEVS] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_conf_counter_sdevs",
		.sd_allowed = M0_BITS(M0_IOS_START_PM_INIT,
				      M0_IOS_START_FAILURE)
	},
	[M0_IOS_START_PM_INIT] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_pm_init",
		.sd_allowed = M0_BITS(M0_IOS_START_CONF_FILL_INIT)
	},
	[M0_IOS_START_CONF_FILL_INIT] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_conf_fill_init",
		.sd_allowed = M0_BITS(M0_IOS_START_CONF_FILL,
				      M0_IOS_START_FAILURE)
	},
	[M0_IOS_START_CONF_FILL] = {
		.sd_flags   = 0,
		.sd_name    = "ios_start_conf_fill",
		.sd_allowed = M0_BITS(M0_IOS_START_COMPLETE,
				      M0_IOS_START_FAILURE)
	},
	[M0_IOS_START_FAILURE] = {
		.sd_flags     = M0_SDF_FAILURE | M0_SDF_FINAL,
		.sd_name      = "ios_start_failure",
	},
	[M0_IOS_START_COMPLETE] = {
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

static void ios_start_buffer_pool_create(struct m0_ios_start_sm *ios_sm);
static int ios_start_fs_obj_open(struct m0_ios_start_sm *ios_sm);
static void ios_start_conf_sdev_init(struct m0_ios_start_sm *ios_sm);
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

static void ios_start_sdev_list_fini(struct m0_ios_start_sm *ios_sm)
{
	struct m0_fid_item *si;

	if (!m0_fids_tlist_is_empty(&ios_sm->ism_sdevs_fid))
		m0_tl_teardown(m0_fids, &ios_sm->ism_sdevs_fid, si) {
			m0_free(si);
		}
	m0_fids_tlist_fini(&ios_sm->ism_sdevs_fid);
}

static void ios_start_sm_failure(struct m0_ios_start_sm *ios_sm, int rc)
{
	enum m0_ios_start_state state = ios_start_state_get(ios_sm);

	switch (state) {
	case M0_IOS_START_INIT:
		/* fallthrough */
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
		/* Possibly errors source: tx close, m0_cob_domain_mkfs */
		m0_cob_domain_destroy(ios_sm->ism_dom, ios_sm->ism_sm.sm_grp);
		m0_cob_domain_fini(ios_sm->ism_dom);
		break;
	case M0_IOS_START_BUFFER_POOL_CREATE:
		m0_cob_domain_destroy(ios_sm->ism_dom, ios_sm->ism_sm.sm_grp);
		m0_cob_domain_fini(ios_sm->ism_dom);
		m0_ios_delete_buffer_pool(ios_sm->ism_service);
		break;
	case M0_IOS_START_COMPLETE:
		/* Possibly errors source: tx close */
		/* fallthrough */
	case M0_IOS_START_CONF_COUNTER_SDEVS:
		/* Possibly errors source: m0_confc_... , pm_prepare */
		/* fallthrough */
	case M0_IOS_START_PM_INIT:
		/* Possibly errors source: tx open */
		/* fallthrough */
	case M0_IOS_START_CONF_FILL_INIT:
		/* Possibly errors source: m0_confc_...  */
		ios_start_sdev_list_fini(ios_sm);
		/* fallthrough */
	case M0_IOS_START_CONF_COUNTER_INIT:
		/* Possibly errors source: m0_confc_...  */
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

/* TX section  */

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

static void ios_start_cdom_tx_open(struct m0_ios_start_sm *ios_sm)
{
	struct m0_be_tx_credit  cred  = {};
	struct m0_be_seg       *seg = ios_sm->ism_reqh->rh_beseg;
	struct m0_be_tx        *tx = &ios_sm->ism_tx;

	m0_cob_domain_credit_add(ios_sm->ism_dom, tx, seg,
				 &ios_sm->ism_cdom_id, &cred);

	M0_SET0(tx);
	m0_be_tx_init(tx, 0, seg->bs_domain, ios_sm->ism_sm.sm_grp,
		      NULL, NULL, NULL, NULL);
	m0_be_tx_prep(tx, &cred);

	ios_start_state_set(ios_sm, M0_IOS_START_CDOM_CREATE);
	ios_start_tx_open(ios_sm, true);
}

/* AST section */

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
			ios_start_state_set(ios_sm,
					    M0_IOS_START_BUFFER_POOL_CREATE);
			ios_start_buffer_pool_create(ios_sm);
		} else {
			ios_start_sm_failure(ios_sm, rc);
		}
	}
	M0_LEAVE();
}

static void ios_start_ast_cdom_create(struct m0_sm_group *grp,
				   struct m0_sm_ast *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);
	int                    rc;

	M0_ENTRY();
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
	M0_LEAVE();
}

static void ios_start_cob_tx_open(struct m0_ios_start_sm *ios_sm)
{
	struct m0_be_tx_credit  cred  = {};
	struct m0_be_seg       *seg = ios_sm->ism_reqh->rh_beseg;

	M0_ENTRY("key init for reqh=%p, key=%d",
		 ios_sm->ism_reqh, m0_get()->i_ios_cdom_key);

	m0_cob_tx_credit(ios_sm->ism_dom, M0_COB_OP_DOMAIN_MKFS, &cred);
	M0_SET0(&ios_sm->ism_tx);
	m0_be_tx_init(&ios_sm->ism_tx, 0, seg->bs_domain,
		      ios_sm->ism_sm.sm_grp, NULL, NULL, NULL, NULL);
	m0_be_tx_prep(&ios_sm->ism_tx, &cred);

	ios_start_tx_open(ios_sm, false);

	M0_LEAVE();
}

static void ios_start_ast_cdom_create_res(struct m0_sm_group *grp,
					  struct m0_sm_ast   *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);

	M0_ENTRY();

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
	M0_LEAVE();
}

static void ios_start_ast_mkfs(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);

	M0_ENTRY();
	M0_ASSERT(ios_start_state_get(ios_sm) == M0_IOS_START_MKFS);

	ios_sm->ism_last_rc = m0_cob_domain_mkfs(ios_sm->ism_dom,
						 &M0_MDSERVICE_SLASH_FID,
						 &ios_sm->ism_tx);

	ios_start_state_set(ios_sm, M0_IOS_START_MKFS_RESULT);
	ios_start_tx_close(ios_sm);
	M0_LEAVE();
}

static void ios_start_ast_mkfs_result(struct m0_sm_group *grp,
				     struct m0_sm_ast *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);

	M0_ENTRY();
	M0_ASSERT(ios_start_state_get(ios_sm) == M0_IOS_START_MKFS_RESULT);

	m0_be_tx_fini(&ios_sm->ism_tx);

	ios_start_state_set(ios_sm, M0_IOS_START_BUFFER_POOL_CREATE);
	ios_start_buffer_pool_create(ios_sm);

	M0_LEAVE();
}

static void ios_start_buffer_pool_create(struct m0_ios_start_sm *ios_sm)
{
	int rc;

	M0_ENTRY();
	M0_ASSERT(ios_start_state_get(ios_sm) ==
		  M0_IOS_START_BUFFER_POOL_CREATE);

	rc = m0_ios_create_buffer_pool(ios_sm->ism_service) ?:
	     ios_start_fs_obj_open(ios_sm);
	if (rc != 0) {
		ios_start_sm_failure(ios_sm, rc);
	}

	M0_LEAVE();
}

/* Conf filesystem section */

static bool ios_start_ast_conf_fs_get_cb(struct m0_clink *cl)
{
	struct m0_ios_start_sm *ios_sm = ios_start_clink2sm(cl);
	int                     rc;

	if (m0_confc_ctx_is_completed(&ios_sm->ism_confc_ctx)) {
		rc = m0_confc_ctx_error(&ios_sm->ism_confc_ctx);
		if (rc != 0) {
			ios_start_sm_failure(ios_sm, rc);
		} else {
			ios_start_state_set(ios_sm,
					    M0_IOS_START_CONF_COUNTER_INIT);
			ios_start_sm_tick(ios_sm);
		}

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
		return M0_ERR(-EPERM);
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

/* Poolmachine section */

static int ios_start_pm_prepare(struct m0_ios_start_sm *ios_sm)
{
	struct m0_be_tx_credit  cred  = {};
	struct m0_be_seg       *seg = ios_sm->ism_reqh->rh_beseg;
	struct m0_be_tx        *tx = &ios_sm->ism_tx;

	M0_ENTRY();

	M0_ALLOC_PTR(ios_sm->ism_poolmach);
	if (ios_sm->ism_poolmach == NULL)
		return M0_ERR(-ENOMEM);

	m0_poolmach_store_init_creds_add(seg,
					 ios_sm->ism_poolmach_args.nr_nodes,
					 ios_sm->ism_poolmach_args.nr_sdevs,
					 ios_sm->ism_poolmach_args.nr_sdevs,
					 &cred);
	M0_SET0(tx);
	m0_be_tx_init(tx, 0, seg->bs_domain, ios_sm->ism_sm.sm_grp,
		      NULL, NULL, NULL, NULL);
	m0_be_tx_prep(tx, &cred);

	ios_start_state_set(ios_sm, M0_IOS_START_PM_INIT);
	ios_start_tx_open(ios_sm, true);
	return M0_RC(0);
}

static void ios_start_ast_pm_init(struct m0_sm_group *grp,
				  struct m0_sm_ast   *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);
	int                     rc;

	M0_ENTRY();
	M0_ASSERT(ios_start_state_get(ios_sm) == M0_IOS_START_PM_INIT);

	rc = m0_poolmach_backed_init(ios_sm->ism_poolmach,
				     NULL,
				     ios_sm->ism_reqh->rh_beseg,
				     &ios_sm->ism_tx,
				     ios_sm->ism_poolmach_args.nr_nodes,
				     ios_sm->ism_poolmach_args.nr_sdevs,
				     PM_DEFAULT_MAX_NODE_FAILURES,
				     ios_sm->ism_poolmach_args.nr_sdevs);
	if (rc != 0)
		ios_sm->ism_last_rc = M0_ERR(rc);

	ios_start_state_set(ios_sm, M0_IOS_START_CONF_FILL_INIT);
	ios_start_tx_close(ios_sm);
	M0_LEAVE();
}

static void ios_start_pm_set(struct m0_ios_start_sm *ios_sm)
{
	M0_ENTRY();
	m0_rwlock_write_lock(&ios_sm->ism_reqh->rh_rwlock);
	m0_reqh_lockers_set(ios_sm->ism_reqh, poolmach_key,
			    ios_sm->ism_poolmach);
	m0_rwlock_write_unlock(&ios_sm->ism_reqh->rh_rwlock);

	ios_start_state_set(ios_sm, M0_IOS_START_COMPLETE);
	M0_LEAVE();
}

/* Conf iter section */

static void ios_start_pm_nodes_counter(struct m0_ios_start_sm *ios_sm,
				       struct m0_conf_obj     *obj)
{
	M0_ASSERT(ios_start_state_get(ios_sm) ==
		  M0_IOS_START_CONF_COUNTER_NODES);

	if (obj != NULL && m0_conf_obj_type(obj) == &M0_CONF_CONTROLLER_TYPE)
			ios_sm->ism_poolmach_args.nr_nodes++;
}

static bool ios_start_sdev_belongs_ioservice(const struct m0_conf_obj *sdev_obj)
{
	struct m0_conf_obj     *svc_obj = m0_conf_obj_grandparent(sdev_obj);
	struct m0_conf_service *svc = M0_CONF_CAST(svc_obj, m0_conf_service);

	return svc != NULL && svc->cs_type == M0_CST_IOS;
}

static int ios_start_pm_sdevs_counter(struct m0_ios_start_sm *ios_sm,
				      struct m0_conf_obj     *obj)
{
	struct m0_fid_item *si;

	M0_ASSERT(ios_start_state_get(ios_sm) ==
		  M0_IOS_START_CONF_COUNTER_SDEVS);

	if (obj != NULL && m0_conf_obj_type(obj) == &M0_CONF_SDEV_TYPE) {
		if (ios_start_sdev_belongs_ioservice(obj)) {

			M0_ALLOC_PTR(si);
			if (si == NULL)
				return M0_ERR(-ENOMEM);
			si->i_fid = obj->co_id;
			m0_fids_tlink_init_at(si, &ios_sm->ism_sdevs_fid);

			ios_sm->ism_poolmach_args.nr_sdevs++;
		}
	}
	return M0_RC(0);
}

static void ios_start_pm_disk_add(struct m0_ios_start_sm *ios_sm,
				  struct m0_conf_obj     *obj)
{
	struct m0_conf_disk *d;
	int                  idx;

	M0_ASSERT(ios_start_state_get(ios_sm) == M0_IOS_START_CONF_FILL);

	if (obj != NULL && m0_conf_obj_type(obj) == &M0_CONF_DISK_TYPE) {
		d = M0_CONF_CAST(obj, m0_conf_disk);

		if (m0_tl_exists(m0_fids, item, &ios_sm->ism_sdevs_fid,
				 m0_fid_eq(&item->i_fid,
					   &d->ck_dev->sd_obj.co_id)))
		{
			idx = ios_sm->ism_pool_index;
			M0_ASSERT(idx < ios_sm->ism_poolmach_args.nr_sdevs);
			ios_sm->ism_poolmach->pm_state->
				pst_devices_array[idx].pd_id = d->ck_obj.co_id;
		}
	}
}

static bool _obj_is_disk_or_controller(const struct m0_conf_obj *obj)
{
	return M0_IN(m0_conf_obj_type(obj),
		     (&M0_CONF_DISK_TYPE, &M0_CONF_SDEV_TYPE,
		      &M0_CONF_CONTROLLER_TYPE));
}

static void ios_start_poolmash_disks_check(struct m0_ios_start_sm *ios_sm)
{
	M0_LOG(M0_DEBUG, "nodes=%d, sdev=%d, disks=%d",
			 ios_sm->ism_poolmach_args.nr_nodes,
			 ios_sm->ism_poolmach_args.nr_sdevs,
			 ios_sm->ism_pool_index);

	if (ios_sm->ism_poolmach_args.nr_sdevs != ios_sm->ism_pool_index) {
		M0_LOG(M0_DEBUG, "Conf has error on this poolmach");
	}
}

static void ios_start_conf_disk_next(struct m0_ios_start_sm *ios_sm)
{
	int rc;
	int rc1 = 0;

	M0_ENTRY();
	while ((rc = m0_conf_diter_next(&ios_sm->ism_it,
					_obj_is_disk_or_controller)) ==
			M0_CONF_DIRNEXT && rc1 == 0) {
		switch (ios_start_state_get(ios_sm)) {
		case M0_IOS_START_CONF_COUNTER_NODES:
			ios_start_pm_nodes_counter(ios_sm,
				      m0_conf_diter_result(&ios_sm->ism_it));
			break;
		case M0_IOS_START_CONF_COUNTER_SDEVS:
			rc1 = ios_start_pm_sdevs_counter(ios_sm,
				      m0_conf_diter_result(&ios_sm->ism_it));
			break;
		case M0_IOS_START_CONF_FILL:
			ios_start_pm_disk_add(ios_sm,
				      m0_conf_diter_result(&ios_sm->ism_it));
			break;
		default:
			M0_IMPOSSIBLE("Invalid phase");
		}
	}
	if (rc1 != 0)
		rc = rc1;

	/*
	 * If rc == M0_CONF_DIRMISS, then ios_start_disk_iter_cb() will be
	 * called once directory entry is loaded
	 */
	if (rc != M0_CONF_DIRMISS) {
		/* End of directory or error */
		m0_clink_del_lock(&ios_sm->ism_clink);
		m0_clink_fini(&ios_sm->ism_clink);
		m0_conf_diter_fini(&ios_sm->ism_it);

		if (rc == M0_CONF_DIREND) {
			rc = 0;
			switch (ios_start_state_get(ios_sm)) {
			case M0_IOS_START_CONF_COUNTER_NODES:
				ios_start_conf_sdev_init(ios_sm);
				break;
			case M0_IOS_START_CONF_COUNTER_SDEVS:
				rc = ios_start_pm_prepare(ios_sm);
				break;
			case M0_IOS_START_CONF_FILL:
				ios_start_poolmash_disks_check(ios_sm);
				ios_start_sdev_list_fini(ios_sm);
				ios_start_fs_obj_close(ios_sm);
				ios_start_pm_set(ios_sm);
				break;
			default:
				/* Unexpected case*/
				M0_ASSERT(true);
			}
		}
		if (rc != 0)
			ios_start_sm_failure(ios_sm, rc);
	}
	M0_LEAVE();
}

static void ios_start_ast_conf_iter(struct m0_sm_group *grp,
				    struct m0_sm_ast   *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);

	M0_ENTRY();
	ios_start_conf_disk_next(ios_sm);
	M0_LEAVE();
}

static bool ios_start_disk_iter_cb(struct m0_clink *cl)
{
	struct m0_ios_start_sm *ios_sm = ios_start_clink2sm(cl);

	ios_start_sm_tick(ios_sm);
	return true;
}

static void ios_start_conf_sdev_init(struct m0_ios_start_sm *ios_sm)
{
	struct m0_confc *confc;
	int              rc;

	m0_fids_tlist_init(&ios_sm->ism_sdevs_fid);

	confc = &ios_sm->ism_reqh->rh_confc;
	rc = m0_conf_diter_init(&ios_sm->ism_it, confc, ios_sm->ism_fs_obj,
				M0_CONF_FILESYSTEM_NODES_FID,
				M0_CONF_NODE_PROCESSES_FID,
				M0_CONF_PROCESS_SERVICES_FID,
				M0_CONF_SERVICE_SDEVS_FID);
	if (rc != 0) {
		ios_start_fs_obj_close(ios_sm);
		ios_start_sm_failure(ios_sm, rc);
		return;
	}
	m0_conf_diter_locked_set(&ios_sm->ism_it, true);

	ios_start_state_set(ios_sm, M0_IOS_START_CONF_COUNTER_SDEVS);

	m0_clink_init(&ios_sm->ism_clink, ios_start_disk_iter_cb);
	m0_clink_add_lock(&ios_sm->ism_it.di_wait, &ios_sm->ism_clink);
	ios_start_conf_disk_next(ios_sm);
}

static void ios_start_conf_iter_init(struct m0_ios_start_sm  *ios_sm,
				     enum m0_ios_start_state  next_state)
{
	struct m0_confc *confc;
	int              rc;

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
	m0_conf_diter_locked_set(&ios_sm->ism_it, true);

	ios_start_state_set(ios_sm, next_state);

	m0_clink_init(&ios_sm->ism_clink, ios_start_disk_iter_cb);
	m0_clink_add_lock(&ios_sm->ism_it.di_wait, &ios_sm->ism_clink);
	ios_start_conf_disk_next(ios_sm);
}

static void ios_start_ast_conf_counter_init(struct m0_sm_group *grp,
					    struct m0_sm_ast   *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);

	M0_ENTRY();
	M0_ASSERT(ios_start_state_get(ios_sm) ==
		  M0_IOS_START_CONF_COUNTER_INIT);

	ios_sm->ism_fs_obj = m0_confc_ctx_result(&ios_sm->ism_confc_ctx);

	ios_sm->ism_poolmach_args.nr_sdevs = 0;
	ios_sm->ism_poolmach_args.nr_nodes = 0;
	ios_start_conf_iter_init(ios_sm, M0_IOS_START_CONF_COUNTER_NODES);
	M0_LEAVE();
}

static void ios_start_ast_conf_fill_init(struct m0_sm_group *grp,
					 struct m0_sm_ast   *ast)
{
	struct m0_ios_start_sm *ios_sm = ios_start_ast2sm(ast);

	M0_ENTRY();
	M0_ASSERT(ios_start_state_get(ios_sm) == M0_IOS_START_CONF_FILL_INIT);

	m0_be_tx_fini(&ios_sm->ism_tx);

	if (ios_sm->ism_last_rc != 0) {
		ios_start_sm_failure(ios_sm, ios_sm->ism_last_rc);
	} else {
		ios_sm->ism_pool_index = 0;
		ios_start_conf_iter_init(ios_sm, M0_IOS_START_CONF_FILL);
	}

	M0_LEAVE();
}

/* SM section */

static void ios_start_sm_tick(struct m0_ios_start_sm *ios_sm)
{
	int                             state;
	void (*handlers[])(struct m0_sm_group *, struct m0_sm_ast *) = {
		[M0_IOS_START_INIT] = ios_start_ast_start,
		[M0_IOS_START_CDOM_CREATE] = ios_start_ast_cdom_create,
		[M0_IOS_START_CDOM_CREATE_RES] = ios_start_ast_cdom_create_res,
		[M0_IOS_START_MKFS] = ios_start_ast_mkfs,
		[M0_IOS_START_MKFS_RESULT] = ios_start_ast_mkfs_result,
		[M0_IOS_START_CONF_COUNTER_INIT] =
						ios_start_ast_conf_counter_init,
		[M0_IOS_START_CONF_COUNTER_NODES] = ios_start_ast_conf_iter,
		[M0_IOS_START_CONF_COUNTER_SDEVS] = ios_start_ast_conf_iter,
		[M0_IOS_START_PM_INIT] = ios_start_ast_pm_init,
		[M0_IOS_START_CONF_FILL_INIT] = ios_start_ast_conf_fill_init,
		[M0_IOS_START_CONF_FILL] = ios_start_ast_conf_iter
	};

	M0_ENTRY();
	state = ios_start_state_get(ios_sm);
	M0_LOG(M0_DEBUG, "State: %d", state);
	M0_ASSERT(handlers[state] != NULL);
	M0_SET0(&ios_sm->ism_ast);
	ios_sm->ism_ast.sa_cb = handlers[state];
	ios_sm->ism_ast.sa_datum = ios_sm;
	m0_sm_ast_post(ios_sm->ism_sm.sm_grp, &ios_sm->ism_ast);
	M0_LEAVE();
}

M0_INTERNAL void m0_ios_start_sm_fini(struct m0_ios_start_sm *ios_sm)
{
	M0_ENTRY("ios_sm: %p", ios_sm);
	M0_PRE(ios_sm != NULL);
	M0_PRE(M0_IN(ios_start_state_get(ios_sm),
		(M0_IOS_START_COMPLETE, M0_IOS_START_FAILURE)));
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
