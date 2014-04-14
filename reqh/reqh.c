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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>,
 *		    Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/19/2010
 */

#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#include "reqh/reqh_addb.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/atomic.h"
#include "lib/locality.h"

#include "mero/magic.h"
#include "stob/stob.h"
#include "net/net.h"
#include "fop/fop.h"
#include "dtm/dtm.h"
#include "rpc/rpc.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "layout/pdclust.h"
#include "mgmt/mgmt.h"

#include "be/ut/helper.h"

/**
   @addtogroup reqh
   @{
 */

/**
   Tlist descriptor for reqh services.
 */
M0_TL_DESCR_DEFINE(m0_reqh_svc, "reqh service", M0_INTERNAL,
		   struct m0_reqh_service, rs_linkage, rs_magix,
		   M0_REQH_SVC_MAGIC, M0_REQH_SVC_HEAD_MAGIC);

M0_TL_DEFINE(m0_reqh_svc, M0_INTERNAL, struct m0_reqh_service);

static struct m0_bob_type rqsvc_bob;
M0_BOB_DEFINE(M0_INTERNAL, &rqsvc_bob, m0_reqh_service);

/**
   Tlist descriptor for rpc machines.
 */
M0_TL_DESCR_DEFINE(m0_reqh_rpc_mach, "rpc machines", ,
		   struct m0_rpc_machine, rm_rh_linkage, rm_magix,
		   M0_RPC_MACHINE_MAGIC, M0_REQH_RPC_MACH_HEAD_MAGIC);

M0_TL_DEFINE(m0_reqh_rpc_mach, , struct m0_rpc_machine);

M0_LOCKERS_DEFINE(M0_INTERNAL, m0_reqh, rh_lockers);

static void __reqh_fini(struct m0_reqh *reqh);

/**
   Request handler state machine description
 */
static struct m0_sm_state_descr m0_reqh_sm_descr[] = {
        [M0_REQH_ST_INIT] = {
                .sd_flags       = M0_SDF_INITIAL | M0_SDF_FINAL,
                .sd_name        = "Init",
                .sd_allowed     = M0_BITS(M0_REQH_ST_MGMT_STARTED,
					  M0_REQH_ST_NORMAL,
					  M0_REQH_ST_SVCS_STOP)
        },
        [M0_REQH_ST_MGMT_STARTED] = {
                .sd_flags       = 0,
                .sd_name        = "ManagementStart",
                .sd_allowed     = M0_BITS(M0_REQH_ST_NORMAL,
					  M0_REQH_ST_SVCS_STOP,
					  M0_REQH_ST_STOPPED)
        },
        [M0_REQH_ST_NORMAL] = {
                .sd_flags       = 0,
                .sd_name        = "Normal",
                .sd_allowed     = M0_BITS(M0_REQH_ST_DRAIN,
					  M0_REQH_ST_SVCS_STOP)
        },
        [M0_REQH_ST_DRAIN] = {
                .sd_flags       = 0,
                .sd_name        = "Drain",
                .sd_allowed     = M0_BITS(M0_REQH_ST_SVCS_STOP)
        },
        [M0_REQH_ST_SVCS_STOP] = {
                .sd_flags       = 0,
                .sd_name        = "ServicesStop",
                .sd_allowed     = M0_BITS(M0_REQH_ST_MGMT_STOP,
					  M0_REQH_ST_STOPPED)
        },
        [M0_REQH_ST_MGMT_STOP] = {
                .sd_flags       = 0,
                .sd_name        = "ManagementStop",
                .sd_allowed     = M0_BITS(M0_REQH_ST_STOPPED)
        },
        [M0_REQH_ST_STOPPED] = {
                .sd_flags       = M0_SDF_TERMINAL,
                .sd_name        = "Stopped",
                .sd_allowed     = 0
        },
};

/**
   Request handler state machine configuration.
 */
static struct m0_sm_conf m0_reqh_sm_conf = {
	.scf_name      = "Request Handler States",
	.scf_nr_states = ARRAY_SIZE(m0_reqh_sm_descr),
	.scf_state     = m0_reqh_sm_descr,
};

M0_INTERNAL bool m0_reqh_invariant(const struct m0_reqh *reqh)
{
	return	reqh != NULL &&
		ergo(M0_IN(reqh->rh_sm.sm_state,(M0_REQH_ST_INIT,
						 M0_REQH_ST_MGMT_STARTED,
						 M0_REQH_ST_NORMAL)),
#ifndef __KERNEL__
		     reqh->rh_fol != NULL &&
#endif
		     reqh->rh_mdstore != NULL) &&
		     m0_fom_domain_invariant(&reqh->rh_fom_dom);
}

M0_INTERNAL int
m0_reqh_init(struct m0_reqh *reqh, const struct m0_reqh_init_args *reqh_args)
{
	int rc;

	reqh->rh_dtm     = reqh_args->rhia_dtm;
	reqh->rh_beseg   = reqh_args->rhia_db;
	reqh->rh_svc     = reqh_args->rhia_svc;
	reqh->rh_mdstore = reqh_args->rhia_mdstore;
	reqh->rh_fol     = reqh_args->rhia_fol;

	if (reqh->rh_fol != NULL)
		reqh->rh_fol->f_reqh = reqh;

	m0_ha_domain_init(&reqh->rh_hadom, M0_HA_EPOCH_NONE);

	m0_addb_mc_init(&reqh->rh_addb_mc);

	/* for UT specifically */
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &reqh->rh_addb_ctx,
			 &m0_addb_ct_reqh_mod, &m0_addb_proc_ctx);

	m0_rwlock_init(&reqh->rh_rwlock);
	m0_reqh_lockers_init(reqh);

	rc = m0_addb_monitors_init(reqh);
	if (rc != 0)
		goto monitors_init_failed;

	reqh->rh_fom_dom.fd_reqh = reqh;
	rc = m0_fom_domain_init(&reqh->rh_fom_dom);
	if (rc != 0)
		goto fom_domain_init_failed;

	m0_reqh_svc_tlist_init(&reqh->rh_services);
	m0_reqh_rpc_mach_tlist_init(&reqh->rh_rpc_machines);
	m0_sm_group_init(&reqh->rh_sm_grp);
	m0_mutex_init(&reqh->rh_mutex); /* deprecated */
	m0_chan_init(&reqh->rh_sd_signal, &reqh->rh_mutex); /* deprecated */
	m0_sm_init(&reqh->rh_sm, &m0_reqh_sm_conf, M0_REQH_ST_INIT,
		   &reqh->rh_sm_grp);

	if (reqh->rh_beseg != NULL) {
		rc = m0_reqh_dbenv_init(reqh, reqh->rh_beseg);
		if (rc != 0)
			__reqh_fini(reqh);
	}
	return rc;

fom_domain_init_failed:
	m0_addb_monitors_fini(reqh);
monitors_init_failed:
	m0_rwlock_fini(&reqh->rh_rwlock);
	m0_reqh_lockers_fini(reqh);
	m0_addb_ctx_fini(&reqh->rh_addb_ctx);
	m0_addb_mc_fini(&reqh->rh_addb_mc);
	m0_ha_domain_fini(&reqh->rh_hadom);
	return rc;
}

#ifndef __KERNEL__
static struct m0_fol *
fol_alloc(struct m0_be_seg *seg)
{
	struct m0_sm_group *grp = m0_locality0_get()->lo_grp;
	struct m0_dtx       tx = {};
	struct m0_fol      *fol;

	m0_sm_group_lock(grp);
	m0_dtx_init(&tx, seg->bs_domain, grp);
	M0_BE_ALLOC_CREDIT_PTR(fol, seg, &tx.tx_betx_cred);
	m0_dtx_open_sync(&tx);
	M0_BE_ALLOC_PTR_SYNC(fol, seg, &tx.tx_betx);
	m0_dtx_done_sync(&tx);
	m0_dtx_fini(&tx);
	m0_sm_group_unlock(grp);

	return fol;
}

static void
fol_free(struct m0_fol *fol, struct m0_be_seg *seg)
{
	struct m0_sm_group *grp = m0_locality0_get()->lo_grp;
	struct m0_dtx       tx = {};

	m0_sm_group_lock(grp);
	m0_dtx_init(&tx, seg->bs_domain, grp);
	M0_BE_FREE_CREDIT_PTR(fol, seg, &tx.tx_betx_cred);
	m0_dtx_open_sync(&tx);
	M0_BE_FREE_PTR_SYNC(fol, seg, &tx.tx_betx);
	m0_dtx_done_sync(&tx);
	m0_dtx_fini(&tx);
	m0_sm_group_unlock(grp);
}

static int fol_create(struct m0_fol *fol, struct m0_be_seg *seg)
{
	struct m0_sm_group *grp = m0_locality0_get()->lo_grp;
	struct m0_dtx       tx = {};
	int                 rc;
	void               *p;

	M0_PRE(m0_be_seg_dict_lookup(seg, "fol", &p) != 0);

	m0_sm_group_lock(grp);
	m0_dtx_init(&tx, seg->bs_domain, grp);
	m0_fol_credit(fol, M0_FO_CREATE, 1, &tx.tx_betx_cred);
	m0_be_seg_dict_insert_credit(seg, "fol", &tx.tx_betx_cred);
	m0_dtx_open_sync(&tx);
	rc = m0_be_seg_dict_insert(seg, &tx.tx_betx, "fol", fol);
	M0_ASSERT(rc == 0);
	M0_BE_OP_SYNC(op, rc = m0_fol_create(fol, &tx.tx_betx, &op));

	m0_dtx_done_sync(&tx);
	m0_dtx_fini(&tx);
	m0_sm_group_unlock(grp);

	return rc;
}

static void fol_destroy(struct m0_fol *fol, struct m0_be_seg *seg)
{
	struct m0_sm_group *grp = m0_locality0_get()->lo_grp;
	struct m0_dtx       tx = {};
	void               *p;

	M0_PRE(m0_be_seg_dict_lookup(seg, "fol", &p) == 0);

	m0_sm_group_lock(grp);
	m0_dtx_init(&tx, seg->bs_domain, grp);
	m0_fol_credit(fol, M0_FO_DESTROY, 1, &tx.tx_betx_cred);
	m0_be_seg_dict_delete_credit(seg, "fol", &tx.tx_betx_cred);
	m0_dtx_open_sync(&tx);
	M0_BE_OP_SYNC(op, m0_fol_destroy(fol, &tx.tx_betx, &op));
	(void) m0_be_seg_dict_delete(seg, &tx.tx_betx, "fol");
	m0_dtx_done_sync(&tx);
	m0_dtx_fini(&tx);
	m0_sm_group_unlock(grp);
}

M0_INTERNAL int m0_reqh_fol_create(struct m0_reqh *reqh,
				   struct m0_be_seg *seg)
{
	int rc;

	M0_ENTRY("reqh=%p", reqh);

	rc = m0_be_seg_dict_lookup(seg, "fol", (void**)&reqh->rh_fol);
	if (rc == 0)
		return M0_RC(-EEXIST);

	reqh->rh_fol = fol_alloc(seg);
	if (reqh->rh_fol == NULL)
		return M0_RC(-ENOMEM);

	m0_fol_init(reqh->rh_fol, seg);
	rc = fol_create(reqh->rh_fol, seg);
	if (rc != 0) {
		fol_free(reqh->rh_fol, seg);
		reqh->rh_fol = NULL;
	}

	return M0_RC(rc);
}

M0_INTERNAL void m0_reqh_fol_destroy(struct m0_reqh *reqh)
{
	M0_PRE(reqh->rh_beseg != NULL);
	M0_PRE(reqh->rh_fol   != NULL);

	fol_destroy(reqh->rh_fol, reqh->rh_beseg);
	m0_fol_fini(reqh->rh_fol);
	fol_free(reqh->rh_fol, reqh->rh_beseg);
	reqh->rh_fol = NULL;
}
#endif

M0_INTERNAL int
m0_reqh_dbenv_init(struct m0_reqh *reqh, struct m0_be_seg *seg)
{
	int rc = 0;

	M0_PRE(seg != NULL);

	M0_ENTRY();

	if (reqh->rh_dbenv != NULL) {
		rc = m0_layout_domain_init(&reqh->rh_ldom, reqh->rh_dbenv);
		if (rc == 0) {
			rc = m0_layout_standard_types_register(&reqh->rh_ldom);
			if (rc != 0)
				m0_layout_domain_fini(&reqh->rh_ldom);
		}
	}

#ifndef __KERNEL__
	rc = m0_be_seg_dict_lookup(seg, "fol", (void**)&reqh->rh_fol);
	if (rc != 0)
		return M0_ERR(rc, "fol not found in BE");
	m0_fol_init(reqh->rh_fol, seg);
	reqh->rh_fol->f_reqh = reqh;
#endif

	reqh->rh_beseg = seg;
	M0_POST(m0_reqh_invariant(reqh));

	return rc;
}

M0_INTERNAL void m0_reqh_dbenv_fini(struct m0_reqh *reqh)
{
	if (reqh->rh_beseg != NULL) {
#ifndef __KERNEL__
		if (reqh->rh_fol != NULL) {
			m0_fol_fini(reqh->rh_fol);
			reqh->rh_fol = NULL;
		}
#endif
		if (reqh->rh_dbenv != NULL) {
			m0_layout_standard_types_unregister(&reqh->rh_ldom);
			m0_layout_domain_fini(&reqh->rh_ldom);
			reqh->rh_dbenv = NULL;
		}
		reqh->rh_beseg = NULL;
	}
	m0_addb_mc_unconfigure(&reqh->rh_addb_mc);
}

static void __reqh_fini(struct m0_reqh *reqh)
{
	m0_sm_group_lock(&reqh->rh_sm_grp);
	m0_sm_fini(&reqh->rh_sm);
	m0_sm_group_unlock(&reqh->rh_sm_grp);
	m0_sm_group_fini(&reqh->rh_sm_grp);
	m0_addb_ctx_fini(&reqh->rh_addb_ctx);
	m0_addb_mc_fini(&reqh->rh_addb_mc);
        m0_fom_domain_fini(&reqh->rh_fom_dom);
	m0_addb_monitors_fini(reqh);
        m0_reqh_svc_tlist_fini(&reqh->rh_services);
        m0_reqh_rpc_mach_tlist_fini(&reqh->rh_rpc_machines);
	m0_reqh_lockers_fini(reqh);
	m0_rwlock_fini(&reqh->rh_rwlock);
	m0_chan_fini_lock(&reqh->rh_sd_signal); /* deprecated */
	m0_mutex_fini(&reqh->rh_mutex); /* deprecated */
	m0_ha_domain_fini(&reqh->rh_hadom);
}

M0_INTERNAL void m0_reqh_fini(struct m0_reqh *reqh)
{
	m0_reqh_dbenv_fini(reqh);
	__reqh_fini(reqh);
}

M0_INTERNAL void m0_reqhs_fini(void)
{
	m0_reqh_service_types_fini();
}

M0_INTERNAL int m0_reqhs_init(void)
{
	m0_reqh_service_types_init();
	m0_addb_ctx_type_register(&m0_addb_ct_reqh_mod);
	m0_bob_type_tlist_init(&rqsvc_bob, &m0_reqh_svc_tl);
	return 0;
}

M0_INTERNAL int
m0_reqh_addb_mc_config(struct m0_reqh *reqh, struct m0_stob *stob)
{
#ifndef __KERNEL__
	/**
	 * @todo Need to replace these 3 with conf parameters and
	 * correct values, these are temporary
	 */
	m0_bcount_t addb_stob_seg_size =
	    M0_RPC_DEF_MAX_RPC_MSG_SIZE * 2;
	m0_bcount_t addb_stob_size = addb_stob_seg_size * 1000;
	m0_time_t   addb_stob_timeout = M0_MKTIME(300, 0); /* 5 mins */
	int rc;

	M0_ENTRY();

	rc = m0_addb_mc_configure_stob_sink(&reqh->rh_addb_mc,
					    reqh,
					    stob,
					    addb_stob_seg_size,
					    addb_stob_size,
					    addb_stob_timeout);
	if (rc != 0)
		return M0_RC(rc);

	m0_addb_mc_configure_pt_evmgr(&reqh->rh_addb_mc);
	if (!m0_addb_mc_is_fully_configured(&m0_addb_gmc))
		m0_addb_mc_dup(&reqh->rh_addb_mc, &m0_addb_gmc);
	m0_addb_ctx_fini(&reqh->rh_addb_ctx);
	M0_ADDB_CTX_INIT(&reqh->rh_addb_mc, &reqh->rh_addb_ctx,
			 &m0_addb_ct_reqh_mod, &m0_addb_proc_ctx);
	return M0_RC(rc);
#else
	return 0;
#endif
}

M0_INTERNAL int m0_reqh_state_get(struct m0_reqh *reqh)
{
	M0_PRE(m0_reqh_invariant(reqh));

	return reqh->rh_sm.sm_state;
}

static void reqh_state_set(struct m0_reqh *reqh,
			   enum m0_reqh_states state)
{
	m0_sm_group_lock(&reqh->rh_sm_grp);
	m0_sm_state_set(&reqh->rh_sm, state);
	m0_sm_group_unlock(&reqh->rh_sm_grp);
}

M0_INTERNAL int m0_reqh_services_state_count(struct m0_reqh *reqh, int state)
{
	int                     cnt = 0;
	struct m0_reqh_service *svc;

	M0_PRE(reqh != NULL);
	M0_PRE(M0_IN(m0_reqh_state_get(reqh),
		     (M0_REQH_ST_MGMT_STARTED, M0_REQH_ST_NORMAL,
		      M0_REQH_ST_DRAIN, M0_REQH_ST_SVCS_STOP)));

	m0_rwlock_read_lock(&reqh->rh_rwlock);
	m0_tl_for(m0_reqh_svc, &reqh->rh_services, svc) {
		if (m0_reqh_service_state_get(svc) == state)
			++cnt;
	} m0_tl_endfor;
	m0_rwlock_read_unlock(&reqh->rh_rwlock);

	return cnt;
}

M0_INTERNAL int m0_reqh_fop_allow(struct m0_reqh *reqh, struct m0_fop *fop)
{
	int                     rh_st;
	int                     svc_st;
	struct m0_reqh_service *svc;

	M0_PRE(reqh != NULL);
	M0_PRE(fop != NULL && fop->f_type != NULL);

	rh_st = m0_reqh_state_get(reqh);
	if (rh_st == M0_REQH_ST_INIT)
		return -EAGAIN;
	if (rh_st == M0_REQH_ST_MGMT_STOP || rh_st == M0_REQH_ST_STOPPED)
		return -ESHUTDOWN;

	/** @deprecated HACK: honor the presence of a local service */
	if (reqh->rh_svc != NULL)
		return 0;
	svc = m0_reqh_service_find(fop->f_type->ft_fom_type.ft_rstype, reqh);
	if (svc == NULL) {
		if (rh_st == M0_REQH_ST_MGMT_STARTED)
			return -EAGAIN;
		return -ECONNREFUSED;
	}
	M0_ASSERT(svc->rs_ops != NULL);
	svc_st = m0_reqh_service_state_get(svc);

	if (rh_st == M0_REQH_ST_NORMAL) {
		if (svc_st == M0_RST_STARTED)
			return 0;
		if (svc_st == M0_RST_STOPPING) {
			if (svc->rs_ops->rso_fop_accept != NULL)
				return (*svc->rs_ops->rso_fop_accept)(svc, fop);
			return -ESHUTDOWN;
		}
		if (svc_st == M0_RST_STARTING)
			return -EBUSY;
		return -ESHUTDOWN;
	}
	if (rh_st == M0_REQH_ST_DRAIN) {
		if (svc->rs_ops->rso_fop_accept != NULL &&
		    (svc_st == M0_RST_STARTED || svc_st == M0_RST_STOPPING))
			return (*svc->rs_ops->rso_fop_accept)(svc, fop);
		return -ESHUTDOWN;
	}
	if (rh_st == M0_REQH_ST_MGMT_STARTED) {
		if (svc == reqh->rh_mgmt_svc) {
			M0_ASSERT(svc->rs_ops->rso_fop_accept != NULL);
			return (*svc->rs_ops->rso_fop_accept)(svc, fop);
		}
		return -EAGAIN;
	}
	if (rh_st == M0_REQH_ST_SVCS_STOP) {
		if (svc == reqh->rh_mgmt_svc) {
			M0_ASSERT(svc->rs_ops->rso_fop_accept != NULL);
			return (*svc->rs_ops->rso_fop_accept)(svc, fop);
		}
		return -ESHUTDOWN;
	}

	return -ENOSYS;
}

M0_INTERNAL int m0_reqh_fop_handle(struct m0_reqh *reqh, struct m0_fop *fop)
{
	struct m0_fom *fom;
	int            rc;

	M0_ENTRY();
	M0_PRE(reqh != NULL);
	M0_PRE(fop != NULL);

	m0_rwlock_read_lock(&reqh->rh_rwlock);

	rc = m0_reqh_fop_allow(reqh, fop);
	if (rc != 0) {
		REQH_ADDB_FUNCFAIL(rc, FOP_HANDLE_2, &reqh->rh_addb_ctx);
		m0_rwlock_read_unlock(&reqh->rh_rwlock);
		return M0_RC(-ESHUTDOWN);
	}

	M0_ASSERT(fop->f_type != NULL);
	M0_ASSERT(fop->f_type->ft_fom_type.ft_ops != NULL);
	M0_ASSERT(fop->f_type->ft_fom_type.ft_ops->fto_create != NULL);

	rc = fop->f_type->ft_fom_type.ft_ops->fto_create(fop, &fom, reqh);
	if (rc == 0) {
		m0_fom_queue(fom, reqh);
	} else {
		REQH_ADDB_FUNCFAIL(rc, FOM_CREATE, &reqh->rh_addb_ctx);
        }

	m0_rwlock_read_unlock(&reqh->rh_rwlock);
	return M0_RC(rc);
}

M0_INTERNAL void m0_reqh_fom_domain_idle_wait(struct m0_reqh *reqh)
{
	struct m0_reqh_service *service;
	struct m0_clink         clink;

	M0_PRE(reqh != NULL);

	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&reqh->rh_sd_signal, &clink);
	m0_tl_for(m0_reqh_svc, &reqh->rh_services, service) {
		M0_ASSERT(m0_reqh_service_invariant(service));

		if ((strcmp(service->rs_type->rst_name, "be-tx-service") == 0))
			continue;
		while (!m0_fom_domain_is_idle_for(&reqh->rh_fom_dom, service))
			m0_chan_wait(&clink);
	} m0_tl_endfor;
	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);
}

M0_INTERNAL void m0_reqh_shutdown(struct m0_reqh *reqh)
{
	struct m0_reqh_service *service;
	struct m0_reqh_service *mdservice = NULL;
	struct m0_reqh_service *rpcservice = NULL;

	M0_PRE(reqh != NULL);
	m0_rwlock_write_lock(&reqh->rh_rwlock);
	M0_PRE(m0_reqh_invariant(reqh));
	M0_PRE(m0_reqh_state_get(reqh) == M0_REQH_ST_NORMAL);
	reqh_state_set(reqh, M0_REQH_ST_DRAIN);
	m0_rwlock_write_unlock(&reqh->rh_rwlock);

	m0_tl_for(m0_reqh_svc, &reqh->rh_services, service) {
		M0_ASSERT(m0_reqh_service_invariant(service));
		if (!M0_IN(m0_reqh_service_state_get(service),
			   (M0_RST_STARTED, M0_RST_STOPPING)))
			continue;
		/* skip mdservice in first loop */
		if ((strcmp(service->rs_type->rst_name, "mdservice") == 0)) {
			mdservice = service;
			continue;
		}
		/* skip rpcservice in first loop */
		if ((strcmp(service->rs_type->rst_name, "rpcservice") == 0)) {
			rpcservice = service;
			continue;
		}
		m0_reqh_service_prepare_to_stop(service);
	} m0_tl_endfor;

	/* notify mdservice */
	if (mdservice != NULL &&
	    M0_IN(m0_reqh_service_state_get(mdservice),
		  (M0_RST_STARTED, M0_RST_STOPPING)))
		m0_reqh_service_prepare_to_stop(mdservice);
	/* notify rpcservice */
	if (rpcservice != NULL &&
	    M0_IN(m0_reqh_service_state_get(rpcservice),
		  (M0_RST_STARTED, M0_RST_STOPPING)))
		m0_reqh_service_prepare_to_stop(rpcservice);
}

M0_INTERNAL void m0_reqh_shutdown_wait(struct m0_reqh *reqh)
{
	m0_reqh_shutdown(reqh);
	m0_reqh_fom_domain_idle_wait(reqh);
}

static void __reqh_svcs_stop(struct m0_reqh *reqh, unsigned level)
{
	struct m0_reqh_service *service;

	m0_tl_for(m0_reqh_svc, &reqh->rh_services, service) {
		M0_ASSERT(m0_reqh_service_invariant(service));
		if (service->rs_level < level)
			continue;
		if (M0_IN(m0_reqh_service_state_get(service),
			  (M0_RST_STARTED, M0_RST_STOPPING)))
			m0_reqh_service_stop(service);
		m0_reqh_service_fini(service);
	} m0_tl_endfor;
}

M0_INTERNAL void m0_reqh_services_terminate(struct m0_reqh *reqh)
{
	m0_reqh_pre_storage_fini_svcs_stop(reqh);
	m0_reqh_post_storage_fini_svcs_stop(reqh);
}

M0_INTERNAL void m0_reqh_pre_storage_fini_svcs_stop(struct m0_reqh *reqh)
{
	M0_PRE(reqh != NULL);
        m0_rwlock_write_lock(&reqh->rh_rwlock);
	M0_PRE(m0_reqh_invariant(reqh));
	M0_PRE(m0_reqh_state_get(reqh) == M0_REQH_ST_DRAIN ||
	       m0_reqh_state_get(reqh) == M0_REQH_ST_NORMAL ||
	       m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STARTED ||
	       m0_reqh_state_get(reqh) == M0_REQH_ST_INIT);

	reqh_state_set(reqh, M0_REQH_ST_SVCS_STOP);
	M0_PRE(m0_reqh_state_get(reqh) == M0_REQH_ST_SVCS_STOP);

        m0_rwlock_write_unlock(&reqh->rh_rwlock);
	__reqh_svcs_stop(reqh, M0_RST_LEVEL_1);

        m0_rwlock_write_lock(&reqh->rh_rwlock);
	if (reqh->rh_mgmt_svc != NULL)
		reqh_state_set(reqh, M0_REQH_ST_MGMT_STOP);
	else
		reqh_state_set(reqh, M0_REQH_ST_STOPPED);
        m0_rwlock_write_unlock(&reqh->rh_rwlock);
}

M0_INTERNAL void m0_reqh_post_storage_fini_svcs_stop(struct m0_reqh *reqh)
{
	M0_PRE(M0_IN(m0_reqh_state_get(reqh), (M0_REQH_ST_MGMT_STOP,
					       M0_REQH_ST_STOPPED)));
	__reqh_svcs_stop(reqh, M0_RST_LEVEL_0);
}

M0_INTERNAL void m0_reqh_start(struct m0_reqh *reqh)
{
	M0_PRE(reqh != NULL);
        m0_rwlock_write_lock(&reqh->rh_rwlock);
	M0_PRE(m0_reqh_invariant(reqh));

	M0_PRE(m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STARTED ||
	       m0_reqh_state_get(reqh) == M0_REQH_ST_INIT);
	reqh_state_set(reqh, M0_REQH_ST_NORMAL);

        m0_rwlock_write_unlock(&reqh->rh_rwlock);
}

M0_INTERNAL int m0_reqh_mgmt_service_start(struct m0_reqh *reqh)
{
	struct m0_reqh_service *service;
	int                     rc;

	M0_PRE(reqh != NULL);
        m0_rwlock_write_lock(&reqh->rh_rwlock);
	M0_PRE(m0_reqh_invariant(reqh));

	M0_PRE(m0_reqh_state_get(reqh) == M0_REQH_ST_INIT);
	M0_PRE(reqh->rh_mgmt_svc == NULL);

	reqh_state_set(reqh, M0_REQH_ST_MGMT_STARTED);
	rc = m0_mgmt_service_allocate(&service);
	if (rc != 0)
		goto allocate_failed;
	m0_reqh_service_init(service, reqh, NULL);
	reqh->rh_mgmt_svc = service;

	m0_rwlock_write_unlock(&reqh->rh_rwlock);

	rc = m0_reqh_service_start(service);
	m0_rwlock_write_lock(&reqh->rh_rwlock);

	if (rc != 0)
		goto start_failed;
	M0_POST(reqh->rh_mgmt_svc == service);
	M0_POST(m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STARTED);
 done:
	m0_rwlock_write_unlock(&reqh->rh_rwlock);
	return rc;

 start_failed:
	reqh->rh_mgmt_svc = NULL;
	m0_reqh_service_fini(service);
 allocate_failed:
	reqh_state_set(reqh, M0_REQH_ST_STOPPED);
	M0_POST(reqh->rh_mgmt_svc == NULL);
	M0_POST(rc != 0);
	goto done;
}

M0_INTERNAL void m0_reqh_mgmt_service_stop(struct m0_reqh *reqh)
{
	struct m0_reqh_service *service;

	M0_PRE(reqh != NULL);
        m0_rwlock_write_lock(&reqh->rh_rwlock);
	M0_PRE(m0_reqh_invariant(reqh));

	M0_PRE(m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STOP);
	service = reqh->rh_mgmt_svc;
	reqh_state_set(reqh, M0_REQH_ST_STOPPED);
        m0_rwlock_write_unlock(&reqh->rh_rwlock);

	if (service != NULL) {
		m0_reqh_service_prepare_to_stop(service);
		m0_reqh_fom_domain_idle_wait(reqh); /* drain mgmt fops */
		m0_reqh_service_stop(service);
		m0_reqh_service_fini(service);
		reqh->rh_mgmt_svc = NULL;
	}
}

M0_INTERNAL void m0_reqh_stats_post_addb(struct m0_reqh *reqh)
{
	int                     i;
	struct m0_reqh_service *service;
	struct m0_rpc_machine  *rpcmach;

	M0_PRE(reqh != NULL);

	m0_rwlock_read_lock(&reqh->rh_rwlock);

	m0_tl_for(m0_reqh_rpc_mach, &reqh->rh_rpc_machines, rpcmach) {
		m0_rpc_machine_stats_post_addb(rpcmach);
		m0_net_tm_stats_post_addb(&rpcmach->rm_tm);
	} m0_tl_endfor;

        m0_tl_for(m0_reqh_svc, &reqh->rh_services, service) {
		M0_ASSERT(m0_reqh_service_invariant(service));
		if (service->rs_ops->rso_stats_post_addb != NULL)
			(*service->rs_ops->rso_stats_post_addb)(service);
	} m0_tl_endfor;

	m0_rwlock_read_unlock(&reqh->rh_rwlock);

	for (i = 0; i < m0_reqh_nr_localities(reqh); i++)
		m0_fom_locality_post_stats(reqh->rh_fom_dom.fd_localities[i]);
}

M0_INTERNAL uint64_t m0_reqh_nr_localities(const struct m0_reqh *reqh)
{
	M0_PRE(m0_reqh_invariant(reqh));

	return reqh->rh_fom_dom.fd_localities_nr;
}

#undef M0_TRACE_SUBSYSTEM
/** @} endgroup reqh */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
