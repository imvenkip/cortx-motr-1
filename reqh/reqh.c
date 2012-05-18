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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/atomic.h"

#include "stob/stob.h"
#include "net/net.h"
#include "fop/fop.h"
#include "fop/fom.h"
#include "fop/fop_iterator.h"
#include "dtm/dtm.h"
#include "fop/fop_format_def.h"
#include "reqh/reqh_service.h"

#include "reqh.h"

/**
   @addtogroup reqh
   @{
 */

/**
 * Reqh addb event location identifier object.
 */
const struct c2_addb_loc c2_reqh_addb_loc = {
	.al_name = "reqh"
};

/**
 * Reqh state of addb context.
 */
const struct c2_addb_ctx_type c2_reqh_addb_ctx_type = {
	.act_name = "reqh"
};

enum {
	REQH_RPC_MACH_HEAD_MAGIX = 0x52455152504D4844 /* REQRPMHD */
};

/**
   Tlist descriptor for reqh services.
 */
C2_TL_DESCR_DEFINE(c2_reqh_svc, "reqh service", , struct c2_reqh_service,
                   rs_linkage, rs_magix, C2_RHS_MAGIX, C2_RHS_MAGIX_HEAD);

C2_TL_DEFINE(c2_reqh_svc, , struct c2_reqh_service);

static struct c2_bob_type rqsvc_bob;
C2_BOB_DEFINE( , &rqsvc_bob, c2_reqh_service);

/**
   Tlist descriptor for rpc machines.
 */
C2_TL_DESCR_DEFINE(c2_reqh_rpc_mach, "rpc machines", , struct c2_rpc_machine,
                   rm_rh_linkage, rm_magix, REQH_RPC_MACH_HEAD_MAGIX, C2_RPC_MACHINE_MAGIX);

C2_TL_DEFINE(c2_reqh_rpc_mach, , struct c2_rpc_machine);

static struct c2_bob_type rqrpm_bob;
C2_BOB_DEFINE( , &rqrpm_bob, c2_rpc_machine);

/**
 * Reqh addb context.
 */
struct c2_addb_ctx c2_reqh_addb_ctx;

#define REQH_ADDB_ADD(addb_ctx, name, rc)  \
C2_ADDB_ADD(&(addb_ctx), &c2_reqh_addb_loc, c2_addb_func_fail, (name), (rc))

extern int c2_reqh_fop_init(void);
extern void c2_reqh_fop_fini(void);

bool c2_reqh_invariant(const struct c2_reqh *reqh)
{
	return reqh != NULL && reqh->rh_stdom != NULL &&
		reqh->rh_dbenv != NULL && reqh->rh_cob_domain != NULL &&
		reqh->rh_fol != NULL &&
		c2_fom_domain_invariant(&reqh->rh_fom_dom);
}

int  c2_reqh_init(struct c2_reqh *reqh, struct c2_dtm *dtm,
                struct c2_stob_domain *stdom, struct c2_dbenv *db,
                struct c2_cob_domain *cdom, struct c2_fol *fol)
{
	int result;

	C2_PRE(reqh != NULL);

	result = c2_fom_domain_init(&reqh->rh_fom_dom);
	if (result == 0) {
		C2_ASSERT(c2_fom_domain_invariant(&reqh->rh_fom_dom));
                reqh->rh_dtm = dtm;
                reqh->rh_stdom = stdom;
                reqh->rh_dbenv = db;
                reqh->rh_cob_domain = cdom;
                reqh->rh_fol = fol;
		reqh->rh_shutdown = false;
                reqh->rh_fom_dom.fd_reqh = reqh;
                c2_reqh_svc_tlist_init(&reqh->rh_services);
                c2_reqh_rpc_mach_tlist_init(&reqh->rh_rpc_machines);
		c2_rwlock_init(&reqh->rh_svcl_rwlock);
		c2_rwlock_init(&reqh->rh_rpcml_rwlock);
		c2_chan_init(&reqh->rh_sd_signal);
		c2_mutex_init(&reqh->rh_lock);
	} else
		REQH_ADDB_ADD(c2_reqh_addb_ctx, "c2_reqh_init", result);

	return result;
}

void c2_reqh_fini(struct c2_reqh *reqh)
{
        C2_PRE(reqh != NULL);
        c2_fom_domain_fini(&reqh->rh_fom_dom);
        c2_reqh_svc_tlist_fini(&reqh->rh_services);
        c2_reqh_rpc_mach_tlist_fini(&reqh->rh_rpc_machines);
	c2_rwlock_fini(&reqh->rh_svcl_rwlock);
	c2_rwlock_fini(&reqh->rh_rpcml_rwlock);
	c2_mutex_fini(&reqh->rh_lock);
}

void c2_reqhs_fini(void)
{
	c2_addb_ctx_fini(&c2_reqh_addb_ctx);
	c2_reqh_service_types_fini();
	c2_reqh_fop_fini();
}

int c2_reqhs_init(void)
{
	c2_addb_ctx_init(&c2_reqh_addb_ctx, &c2_reqh_addb_ctx_type,
					&c2_addb_global_ctx);
	c2_reqh_service_types_init();
	c2_bob_type_tlist_init(&rqsvc_bob, &c2_reqh_svc_tl);
	c2_bob_type_tlist_init(&rqrpm_bob, &c2_reqh_rpc_mach_tl);
	return c2_reqh_fop_init();
}

static void queueit(struct c2_sm_group *grp, struct c2_sm_ast *ast)
{
	struct c2_fom *fom = container_of(ast, struct c2_fom, fo_cb.fc_ast);

	c2_fom_queue(fom);
}

void c2_reqh_fop_handle(struct c2_reqh *reqh,  struct c2_fop *fop)
{
	struct c2_fom	       *fom;
	struct c2_fom_domain   *dom;
	int			result;
	size_t			loc_idx;
	bool                    rsd;

	C2_PRE(reqh != NULL);
	C2_PRE(fop != NULL);

	c2_mutex_lock(&reqh->rh_lock);
	rsd = reqh->rh_shutdown;
	c2_mutex_unlock(&reqh->rh_lock);
	if (rsd) {
		REQH_ADDB_ADD(c2_reqh_addb_ctx, "c2_reqh_fop_handle",
                              ESHUTDOWN);
		return;
	}

	C2_ASSERT(fop->f_type != NULL);
	C2_ASSERT(fop->f_type->ft_fom_type.ft_ops != NULL);
	C2_ASSERT(fop->f_type->ft_fom_type.ft_ops->fto_create != NULL);

	result = fop->f_type->ft_fom_type.ft_ops->fto_create(fop, &fom);
	if (result == 0) {
		C2_ASSERT(fom != NULL);

                /**
                 * To access service specific data,
                 * FOM needs pointer to service instance.
                 */
                if (fom->fo_ops->fo_service_name != NULL) {
                        const char *service_name = NULL;
                        service_name = fom->fo_ops->fo_service_name(fom);
                        fom->fo_service = c2_reqh_service_get(service_name,
							      reqh);
                }
		fom->fo_fol = reqh->rh_fol;
		dom = &reqh->rh_fom_dom;

		loc_idx = fom->fo_ops->fo_home_locality(fom) %
		          dom->fd_localities_nr;
		C2_ASSERT(loc_idx >= 0 && loc_idx < dom->fd_localities_nr);
		fom->fo_loc = &reqh->rh_fom_dom.fd_localities[loc_idx];
		fom->fo_cb.fc_ast.sa_cb = queueit;
		c2_sm_ast_post(&fom->fo_loc->fl_group, &fom->fo_cb.fc_ast);
	} else
		REQH_ADDB_ADD(c2_reqh_addb_ctx, "c2_reqh_fop_handle", result);
}

struct c2_reqh_service *c2_reqh_service_get(const char *service_name,
                                            struct c2_reqh *reqh)
{
	struct c2_reqh_service *service;

	C2_PRE(reqh != NULL && service_name != NULL);

	c2_rwlock_read_lock(&reqh->rh_svcl_rwlock);
	c2_tl_for(c2_reqh_svc, &reqh->rh_services, service) {
		C2_ASSERT(c2_reqh_service_invariant(service));
		if (strcmp(service->rs_type->rst_name, service_name) == 0) {
			c2_rwlock_read_unlock(&reqh->rh_svcl_rwlock);
			return service;
		}
	} c2_tl_endfor;
	c2_rwlock_read_unlock(&reqh->rh_svcl_rwlock);

	return service;
}

void c2_reqh_shutdown_wait(struct c2_reqh *reqh)
{
	struct c2_clink clink;

        c2_clink_init(&clink, NULL);
        c2_clink_add(&reqh->rh_sd_signal, &clink);

	while (c2_atomic64_get(&reqh->rh_fom_dom.fd_foms_nr) > 0)
		c2_chan_wait(&clink);
}

struct c2_rpc_machine *c2_reqh_rpc_machine_get(struct c2_reqh *reqh,
                                                const struct c2_net_xprt *xprt)
{
	struct c2_rpc_machine *rpcmach;
	struct c2_net_xprt    *nxprt;

	C2_PRE(reqh != NULL && xprt != NULL);

	c2_rwlock_read_lock(&reqh->rh_rpcml_rwlock);
	c2_tl_for(c2_reqh_rpc_mach, &reqh->rh_rpc_machines,
						      rpcmach) {
		C2_ASSERT(c2_rpc_machine_bob_check(rpcmach));
		nxprt = rpcmach->rm_tm.ntm_dom->nd_xprt;
		C2_ASSERT(nxprt != NULL);
		if (strcmp(nxprt->nx_name, xprt->nx_name) == 0) {
			c2_rwlock_read_unlock(&reqh->rh_rpcml_rwlock);
			return rpcmach;
		}
	} c2_tl_endfor;
	c2_rwlock_read_unlock(&reqh->rh_rpcml_rwlock);

	return rpcmach;
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
