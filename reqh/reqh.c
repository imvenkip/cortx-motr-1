/* -*- C -*- */
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>,
 *		    Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/19/2010
 */

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
 * Reqh addb event location identifier object.
 */
static const struct m0_addb_loc reqh_addb_loc = {
	.al_name = "reqh"
};

/**
 * Reqh state of addb context.
 */
static const struct m0_addb_ctx_type reqh_addb_ctx_type = {
	.act_name = "reqh"
};

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

#define REQH_ADDB_ADD(addb_ctx, name, rc)  \
M0_ADDB_ADD((addb_ctx), &reqh_addb_loc, m0_addb_func_fail, (name), (rc))

M0_INTERNAL bool m0_reqh_invariant(const struct m0_reqh *reqh)
{
	return reqh != NULL && reqh->rh_dbenv != NULL &&
		reqh->rh_mdstore != NULL && reqh->rh_fol != NULL &&
		m0_fom_domain_invariant(&reqh->rh_fom_dom);
}

M0_INTERNAL int m0_reqh_init(struct m0_reqh *reqh, struct m0_dtm *dtm,
			     struct m0_dbenv *db, struct m0_mdstore *mdstore,
			     struct m0_fol *fol, struct m0_local_service *svc)
{
	int result;

	M0_PRE(reqh != NULL);

	result = m0_fom_domain_init(&reqh->rh_fom_dom);
	if (result != 0)
		return result;
        reqh->rh_dtm = dtm;
        reqh->rh_dbenv = db;
        reqh->rh_svc = svc;
        reqh->rh_mdstore = mdstore;
        reqh->rh_fol = fol;
        reqh->rh_shutdown = false;
        reqh->rh_fom_dom.fd_reqh = reqh;

	m0_addb_ctx_init(&reqh->rh_addb, &reqh_addb_ctx_type,
			 &m0_addb_global_ctx);
	m0_reqh_svc_tlist_init(&reqh->rh_services);
	m0_reqh_rpc_mach_tlist_init(&reqh->rh_rpc_machines);
	m0_chan_init(&reqh->rh_sd_signal);
	m0_rwlock_init(&reqh->rh_rwlock);
	M0_POST(m0_reqh_invariant(reqh));

	return result;
}

M0_INTERNAL void m0_reqh_fini(struct m0_reqh *reqh)
{
        M0_PRE(reqh != NULL);
	m0_addb_ctx_fini(&reqh->rh_addb);
        m0_fom_domain_fini(&reqh->rh_fom_dom);
        m0_reqh_svc_tlist_fini(&reqh->rh_services);
        m0_reqh_rpc_mach_tlist_fini(&reqh->rh_rpc_machines);
	m0_chan_fini(&reqh->rh_sd_signal);
	m0_rwlock_fini(&reqh->rh_rwlock);
}

M0_INTERNAL void m0_reqhs_fini(void)
{
	m0_reqh_service_types_fini();
}

M0_INTERNAL int m0_reqhs_init(void)
{
	m0_reqh_service_types_init();
	m0_bob_type_tlist_init(&rqsvc_bob, &m0_reqh_svc_tl);
	return 0;
}

M0_INTERNAL void m0_reqh_fop_handle(struct m0_reqh *reqh, struct m0_fop *fop)
{
	struct m0_fom	       *fom;
	int			result;
	bool                    rsd;

	M0_PRE(reqh != NULL);
	M0_PRE(fop != NULL);

	m0_rwlock_read_lock(&reqh->rh_rwlock);

	rsd = reqh->rh_shutdown;
	if (rsd) {
		REQH_ADDB_ADD(&reqh->rh_addb, "m0_reqh_fop_handle", ESHUTDOWN);
		m0_rwlock_read_unlock(&reqh->rh_rwlock);
		return;
	}

	M0_ASSERT(fop->f_type != NULL);
	M0_ASSERT(fop->f_type->ft_fom_type.ft_ops != NULL);
	M0_ASSERT(fop->f_type->ft_fom_type.ft_ops->fto_create != NULL);

	result = fop->f_type->ft_fom_type.ft_ops->fto_create(fop, &fom);
	if (result == 0) {
		m0_fom_queue(fom, reqh);
	} else {
		REQH_ADDB_ADD(&reqh->rh_addb, "m0_reqh_fop_handle", result);
        }

	m0_rwlock_read_unlock(&reqh->rh_rwlock);
}

M0_INTERNAL void m0_reqh_shutdown_wait(struct m0_reqh *reqh)
{
	struct m0_clink clink;

        m0_rwlock_write_lock(&reqh->rh_rwlock);
        reqh->rh_shutdown = true;
        m0_rwlock_write_unlock(&reqh->rh_rwlock);

        m0_clink_init(&clink, NULL);
        m0_clink_add(&reqh->rh_sd_signal, &clink);

	while (!m0_fom_domain_is_idle(&reqh->rh_fom_dom))
		m0_chan_wait(&clink);

	m0_clink_del(&clink);
	m0_clink_fini(&clink);
}

M0_INTERNAL uint64_t m0_reqh_nr_localities(const struct m0_reqh *reqh)
{
	M0_PRE(m0_reqh_invariant(reqh));

	return reqh->rh_fom_dom.fd_localities_nr;
}

static unsigned keymax = 0;

M0_INTERNAL unsigned m0_reqh_key_init()
{
	M0_PRE(keymax < REQH_KEY_MAX - 1);
	return keymax++;
}

M0_INTERNAL void *m0_reqh_key_find(struct m0_reqh *reqh, unsigned key,
				   m0_bcount_t size)
{
	void **data;

	M0_PRE(IS_IN_ARRAY(key, reqh->rh_key) && reqh != NULL && size > 0);
	M0_PRE(key <= keymax);

	data = &reqh->rh_key[key];
	if (*data == NULL)
		M0_ALLOC_ADDB(*data, size, &reqh->rh_addb, &reqh_addb_loc);
	return *data;
}

M0_INTERNAL void m0_reqh_key_fini(struct m0_reqh *reqh, unsigned key)
{
	M0_PRE(IS_IN_ARRAY(key, reqh->rh_key));
	m0_free(reqh->rh_key[key]);
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
