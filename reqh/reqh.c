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

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/atomic.h"

#include "mero/magic.h"
#include "stob/stob.h"
#include "net/net.h"
#include "fop/fop.h"
#include "dtm/dtm.h"
#include "rpc/rpc.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"


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

M0_INTERNAL bool m0_reqh_invariant(const struct m0_reqh *reqh)
{
	return reqh != NULL && reqh->rh_dbenv != NULL &&
		reqh->rh_mdstore != NULL && reqh->rh_fol != NULL &&
		m0_fom_domain_invariant(&reqh->rh_fom_dom);
}

M0_INTERNAL int m0_reqh_init(struct m0_reqh *reqh,
			     const struct m0_reqh_init_args *reqh_args)
{
	int         result;

	M0_PRE(reqh != NULL);

	result = m0_fom_domain_init(&reqh->rh_fom_dom);
	if (result != 0)
		return result;
	reqh->rh_dtm             = reqh_args->rhia_dtm;
	reqh->rh_dbenv           = reqh_args->rhia_db;
	reqh->rh_svc             = reqh_args->rhia_svc;
	reqh->rh_mdstore         = reqh_args->rhia_mdstore;
	reqh->rh_fol             = reqh_args->rhia_fol;
	reqh->rh_shutdown        = false;
	reqh->rh_fom_dom.fd_reqh = reqh;

	m0_addb_mc_init(&reqh->rh_addb_mc);

	/** @todo Currently passing dbenv to this api, the duty of the
	    thread doing the io is to create/use a local/embedded m0_dtx
	    and do m0_db_tx_init() of its m0_db_tx member and then do the
	    io (invocation of m0_stob_io_launch(), etc) apis. Need to validate
	    this.
	 */
	if (reqh_args->rhia_addb_stob != NULL) {
#ifndef __KERNEL__
		/**
		 * @todo Need to replace these 3 with conf parameters and
		 * correct values, these are temporary
		 */
		m0_bcount_t addb_stob_seg_size =
		    M0_RPC_DEF_MAX_RPC_MSG_SIZE * 2;
		m0_bcount_t addb_stob_size = addb_stob_seg_size * 1000;
		m0_time_t   addb_stob_timeout = M0_MKTIME(300, 0); /* 5 mins */

		result = m0_addb_mc_configure_stob_sink(&reqh->rh_addb_mc,
						reqh_args->rhia_addb_stob,
						addb_stob_seg_size,
						addb_stob_size,
						addb_stob_timeout);
		if (result != 0)
			return result;

		m0_addb_mc_configure_pt_evmgr(&reqh->rh_addb_mc);
		if (!m0_addb_mc_is_fully_configured(&m0_addb_gmc))
			m0_addb_mc_dup(&reqh->rh_addb_mc, &m0_addb_gmc);
		M0_ADDB_CTX_INIT(&reqh->rh_addb_mc, &reqh->rh_addb_ctx,
				 &m0_addb_ct_reqh_mod, &m0_addb_proc_ctx);
#else
		M0_ASSERT(reqh_args->rhia_addb_stob);
#endif
	} else { /** For UT specifically */
		M0_ADDB_CTX_INIT(&m0_addb_gmc, &reqh->rh_addb_ctx,
				 &m0_addb_ct_reqh_mod, &m0_addb_proc_ctx);
	}

	m0_reqh_svc_tlist_init(&reqh->rh_services);
	m0_reqh_rpc_mach_tlist_init(&reqh->rh_rpc_machines);
	m0_mutex_init(&reqh->rh_mutex);
	m0_chan_init(&reqh->rh_sd_signal, &reqh->rh_mutex);
	m0_rwlock_init(&reqh->rh_rwlock);
	m0_reqh_lockers_init(reqh);
	M0_POST(m0_reqh_invariant(reqh));

	return result;
}

M0_INTERNAL void m0_reqh_fini(struct m0_reqh *reqh)
{
        M0_PRE(reqh != NULL);
	m0_addb_ctx_fini(&reqh->rh_addb_ctx);
	m0_addb_mc_fini(&reqh->rh_addb_mc);
        m0_fom_domain_fini(&reqh->rh_fom_dom);
        m0_reqh_svc_tlist_fini(&reqh->rh_services);
        m0_reqh_rpc_mach_tlist_fini(&reqh->rh_rpc_machines);
	m0_chan_fini_lock(&reqh->rh_sd_signal);
	m0_reqh_lockers_fini(reqh);
	m0_rwlock_fini(&reqh->rh_rwlock);
	m0_chan_fini_lock(&reqh->rh_sd_signal);
	m0_mutex_fini(&reqh->rh_mutex);
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

M0_INTERNAL void m0_reqh_fop_handle(struct m0_reqh *reqh, struct m0_fop *fop)
{
	struct m0_fom *fom;
	int	       result;
	bool           rsd;

	M0_PRE(reqh != NULL);
	M0_PRE(fop != NULL);

	m0_rwlock_read_lock(&reqh->rh_rwlock);

	rsd = reqh->rh_shutdown;
	if (rsd) {
		REQH_ADDB_FUNCFAIL(-ESHUTDOWN, FOP_HANDLE_2,
				   &reqh->rh_addb_ctx);
		m0_rwlock_read_unlock(&reqh->rh_rwlock);
		return;
	}

	M0_ASSERT(fop->f_type != NULL);
	M0_ASSERT(fop->f_type->ft_fom_type.ft_ops != NULL);
	M0_ASSERT(fop->f_type->ft_fom_type.ft_ops->fto_create != NULL);

	result = fop->f_type->ft_fom_type.ft_ops->fto_create(fop, &fom, reqh);
	if (result == 0) {
		m0_fom_queue(fom, reqh);
	} else {
		REQH_ADDB_FUNCFAIL(result, FOM_CREATE, &reqh->rh_addb_ctx);
        }

	m0_rwlock_read_unlock(&reqh->rh_rwlock);
}

M0_INTERNAL void m0_reqh_fom_domain_idle_wait(struct m0_reqh *reqh)
{
	struct m0_clink clink;

	M0_PRE(reqh != NULL);
        m0_clink_init(&clink, NULL);
        m0_clink_add_lock(&reqh->rh_sd_signal, &clink);

	while (!m0_fom_domain_is_idle(&reqh->rh_fom_dom))
		m0_chan_wait(&clink);

	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);
}

M0_INTERNAL void m0_reqh_shutdown_wait(struct m0_reqh *reqh)
{
	struct m0_reqh_service *service;

	M0_PRE(reqh != NULL);
        m0_rwlock_write_lock(&reqh->rh_rwlock);
        reqh->rh_shutdown = true;
        m0_rwlock_write_unlock(&reqh->rh_rwlock);

	m0_tl_for(m0_reqh_svc, &reqh->rh_services, service) {
		M0_ASSERT(m0_reqh_service_invariant(service));
		m0_reqh_service_prepare_to_stop(service);
	} m0_tl_endfor;

	m0_reqh_fom_domain_idle_wait(reqh);
}

M0_INTERNAL void m0_reqh_services_terminate(struct m0_reqh *reqh)
{
	struct m0_reqh_service *service;

	M0_PRE(reqh != NULL);
	m0_tl_for(m0_reqh_svc, &reqh->rh_services, service) {
		M0_ASSERT(m0_reqh_service_invariant(service));
		m0_reqh_service_stop(service);
		m0_reqh_service_fini(service);
	} m0_tl_endfor;
}

M0_INTERNAL void m0_reqh_stats_post_addb(struct m0_reqh *reqh)
{
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
}

M0_INTERNAL uint64_t m0_reqh_nr_localities(const struct m0_reqh *reqh)
{
	M0_PRE(m0_reqh_invariant(reqh));

	return reqh->rh_fom_dom.fd_localities_nr;
}

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
