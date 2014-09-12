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
#include "fop/fom_generic.h"
#include "dtm/dtm.h"
#include "rpc/rpc.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "layout/pdclust.h"
#include "fop/fom_simple.h"

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

struct disallowed_fop_reply {
	struct m0_fom_simple  ffr_sfom;
	struct m0_fop        *ffr_fop;
	int                   ffr_rc;
};

/**
   Request handler state machine description
 */
static struct m0_sm_state_descr m0_reqh_sm_descr[] = {
        [M0_REQH_ST_INIT] = {
                .sd_flags       = M0_SDF_INITIAL | M0_SDF_FINAL,
                .sd_name        = "Init",
                .sd_allowed     = M0_BITS(M0_REQH_ST_NORMAL,
					  M0_REQH_ST_SVCS_STOP)
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
						 M0_REQH_ST_NORMAL)),
		     reqh->rh_mdstore != NULL) &&
		     m0_fom_domain_invariant(&reqh->rh_fom_dom);
}

M0_INTERNAL int
m0_reqh_init(struct m0_reqh *reqh, const struct m0_reqh_init_args *reqh_args)
{
	int rc;

	reqh->rh_dtm     = reqh_args->rhia_dtm;
	reqh->rh_beseg   = reqh_args->rhia_db;
	reqh->rh_mdstore = reqh_args->rhia_mdstore;

	m0_fol_init(&reqh->rh_fol);
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
	m0_sm_init(&reqh->rh_sm, &m0_reqh_sm_conf, M0_REQH_ST_INIT,
		   &reqh->rh_sm_grp);

	if (reqh->rh_beseg != NULL) {
		rc = m0_reqh_dbenv_init(reqh, reqh->rh_beseg);
		if (rc != 0)
			__reqh_fini(reqh);
	}
	return rc;

fom_domain_init_failed:
	m0_addb_monitors_fini(&reqh->rh_addb_monitoring_ctx);
monitors_init_failed:
	m0_rwlock_fini(&reqh->rh_rwlock);
	m0_reqh_lockers_fini(reqh);
	m0_addb_ctx_fini(&reqh->rh_addb_ctx);
	m0_addb_mc_fini(&reqh->rh_addb_mc);
	m0_ha_domain_fini(&reqh->rh_hadom);
	return rc;
}

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

	reqh->rh_beseg = seg;
	M0_POST(m0_reqh_invariant(reqh));

	return rc;
}

M0_INTERNAL void m0_reqh_dbenv_fini(struct m0_reqh *reqh)
{
	if (reqh->rh_beseg != NULL) {
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
	m0_addb_monitors_fini(&reqh->rh_addb_monitoring_ctx);
        m0_reqh_svc_tlist_fini(&reqh->rh_services);
        m0_reqh_rpc_mach_tlist_fini(&reqh->rh_rpc_machines);
	m0_reqh_lockers_fini(reqh);
	m0_rwlock_fini(&reqh->rh_rwlock);
	m0_ha_domain_fini(&reqh->rh_hadom);
	m0_fol_fini(&reqh->rh_fol);
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
	M0_PRE(M0_IN(m0_reqh_state_get(reqh), (M0_REQH_ST_NORMAL,
					       M0_REQH_ST_DRAIN,
					       M0_REQH_ST_SVCS_STOP)));
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
	if (rh_st == M0_REQH_ST_STOPPED)
		return -ESHUTDOWN;

	svc = m0_reqh_service_find(fop->f_type->ft_fom_type.ft_rstype, reqh);
	if (svc == NULL)
		return -ECONNREFUSED;

	M0_ASSERT(svc->rs_ops != NULL);
	svc_st = m0_reqh_service_state_get(svc);

	switch (rh_st) {
	case M0_REQH_ST_NORMAL:
		if (svc_st == M0_RST_STARTED)
			return 0;
		if (svc_st == M0_RST_STARTING)
			return -EBUSY;
		else if (svc_st == M0_RST_STOPPING &&
			 svc->rs_ops->rso_fop_accept != NULL)
			return (*svc->rs_ops->rso_fop_accept)(svc, fop);
		return -ESHUTDOWN;
	case M0_REQH_ST_DRAIN:
		if (M0_IN(svc_st, (M0_RST_STARTED, M0_RST_STOPPING)) &&
		    svc->rs_ops->rso_fop_accept != NULL)
			return (*svc->rs_ops->rso_fop_accept)(svc, fop);
		return -ESHUTDOWN;
	case M0_REQH_ST_SVCS_STOP:
		return rh_st == -ESHUTDOWN;
	default:
		return -ENOSYS;
	};
}


static int disallowed_fop_tick(struct m0_fom *fom, void *data, int *phase)
{
	struct m0_fop               *fop;
	struct disallowed_fop_reply *reply = data;
	struct m0_addb_ctx          *ctx;
	struct m0_reqh              *reqh = fom->fo_loc->fl_dom->fd_reqh;
	static const char            msg[] = "No service running.";

	ctx = reqh != NULL ? &reqh->rh_addb_ctx : &m0_addb_proc_ctx;
	fop = m0_fop_reply_alloc(reply->ffr_fop, &m0_fop_generic_reply_fopt);
        if (fop == NULL)
                REQH_ADDB_OOM(FOP_FAILED_REPLY_TICK_1, ctx);
	else {
		struct m0_fop_generic_reply *rep = m0_fop_data(fop);

		rep->gr_rc = reply->ffr_rc;
		rep->gr_msg.s_buf = m0_alloc(sizeof msg);
		if (rep->gr_msg.s_buf != NULL) {
			rep->gr_msg.s_len = sizeof msg;
			memcpy(rep->gr_msg.s_buf, msg, rep->gr_msg.s_len);
		}
		m0_rpc_reply_post(&reply->ffr_fop->f_item, &fop->f_item);
	}

	return -1;
}

static void disallowed_fop_free(struct m0_fom_simple *sfom)
{
	struct disallowed_fop_reply *reply;

	reply = container_of(sfom, struct disallowed_fop_reply, ffr_sfom);
	m0_free(reply);
}

static void fop_disallowed(struct m0_reqh *reqh,
				  struct m0_fop  *req_fop,
				  int             rc)
{
	struct disallowed_fop_reply *reply;

	M0_PRE(rc != 0);
	M0_PRE(req_fop != NULL);

	REQH_ALLOC_PTR(reply, &reqh->rh_addb_ctx, FOP_FAILED_REPLY_1);
	if (reply == NULL)
		return;

        m0_fop_get(req_fop);
	reply->ffr_fop = req_fop;
	reply->ffr_rc = rc;
	M0_FOM_SIMPLE_POST(&reply->ffr_sfom, reqh, NULL, disallowed_fop_tick,
			   disallowed_fop_free, reply, M0_FOM_SIMPLE_HERE);

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
		M0_LOG(M0_WARN, "fop \"%s\"@%p disallowed: %i.",
		       m0_fop_name(fop), fop, rc);
		fop_disallowed(reqh, fop, -ESHUTDOWN);
		/*
		 * Note :
		 *      Since client will receive generic reply for
		 *      this error, for RPC layer this fop is accepted.
		 */
		return M0_RC(0);
	}

	M0_ASSERT(fop->f_type != NULL);
	M0_ASSERT(fop->f_type->ft_fom_type.ft_ops != NULL);
	M0_ASSERT(fop->f_type->ft_fom_type.ft_ops->fto_create != NULL);

	rc = fop->f_type->ft_fom_type.ft_ops->fto_create(fop, &fom, reqh);
	if (rc == 0)
		m0_fom_queue(fom, reqh);
	else
		REQH_ADDB_FUNCFAIL(rc, FOM_CREATE, &reqh->rh_addb_ctx);

	m0_rwlock_read_unlock(&reqh->rh_rwlock);
	return M0_RC(rc);
}

M0_INTERNAL void m0_reqh_idle_wait_for(struct m0_reqh *reqh,
				       struct m0_reqh_service *service)
{
	struct m0_clink clink;

	M0_PRE(m0_reqh_service_invariant(service));

	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&reqh->rh_sm_grp.s_chan, &clink);
	while (!m0_fom_domain_is_idle_for(&reqh->rh_fom_dom, service))
		m0_chan_wait(&clink);
	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);
}

M0_INTERNAL void m0_reqh_idle_wait(struct m0_reqh *reqh)
{
	struct m0_reqh_service *service;

	M0_PRE(reqh != NULL);

	m0_tl_for(m0_reqh_svc, &reqh->rh_services, service) {
		M0_ASSERT(m0_reqh_service_invariant(service));
		if (service->rs_level < 2)
			continue;
		m0_reqh_idle_wait_for(reqh, service);
	} m0_tl_endfor;
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
	m0_reqh_idle_wait(reqh);
}

static void __reqh_svcs_stop(struct m0_reqh *reqh, unsigned level)
{
	struct m0_reqh_service *service;

	m0_tl_for(m0_reqh_svc, &reqh->rh_services, service) {
		M0_ASSERT(m0_reqh_service_invariant(service));
		if (service->rs_level < level)
			continue;
		if (M0_IN(m0_reqh_service_state_get(service),
			  (M0_RST_STARTED, M0_RST_STOPPING))) {
			m0_reqh_service_prepare_to_stop(service);
			m0_reqh_idle_wait_for(reqh, service);
			m0_reqh_service_stop(service);
		}
		m0_reqh_service_fini(service);
		if (service == reqh->rh_rpc_service)
			reqh->rh_rpc_service = NULL;
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
	M0_PRE(M0_IN(m0_reqh_state_get(reqh),
		     (M0_REQH_ST_DRAIN, M0_REQH_ST_NORMAL, M0_REQH_ST_INIT)));

	reqh_state_set(reqh, M0_REQH_ST_SVCS_STOP);
	M0_PRE(m0_reqh_state_get(reqh) == M0_REQH_ST_SVCS_STOP);

        m0_rwlock_write_unlock(&reqh->rh_rwlock);
	__reqh_svcs_stop(reqh, 2);

        m0_rwlock_write_lock(&reqh->rh_rwlock);
	reqh_state_set(reqh, M0_REQH_ST_STOPPED);
        m0_rwlock_write_unlock(&reqh->rh_rwlock);
}

M0_INTERNAL void m0_reqh_post_storage_fini_svcs_stop(struct m0_reqh *reqh)
{
	M0_PRE(m0_reqh_state_get(reqh) == M0_REQH_ST_STOPPED);
	__reqh_svcs_stop(reqh, 1);
}

M0_INTERNAL void m0_reqh_start(struct m0_reqh *reqh)
{
	M0_PRE(reqh != NULL);
        m0_rwlock_write_lock(&reqh->rh_rwlock);
	M0_PRE(m0_reqh_invariant(reqh));

	M0_PRE(m0_reqh_state_get(reqh) == M0_REQH_ST_INIT);
	reqh_state_set(reqh, M0_REQH_ST_NORMAL);

        m0_rwlock_write_unlock(&reqh->rh_rwlock);
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
