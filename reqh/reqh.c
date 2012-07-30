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
#include "rpc/rpc2.h"
#include "reqh/reqh_service.h"

#include "reqh/reqh.h"

/**
   @addtogroup reqh
   @{
 */

/**
 * Reqh addb event location identifier object.
 */
static const struct c2_addb_loc reqh_addb_loc = {
	.al_name = "reqh"
};

/**
 * Reqh state of addb context.
 */
static const struct c2_addb_ctx_type reqh_addb_ctx_type = {
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

struct c2_bob_type rqrpm_bob;
C2_BOB_DEFINE( , &rqrpm_bob, c2_rpc_machine);


/**
 * Reqh addb context.
 */
static struct c2_addb_ctx reqh_addb_ctx;

#define REQH_ADDB_ADD(addb_ctx, name, rc)  \
C2_ADDB_ADD((addb_ctx), &reqh_addb_loc, c2_addb_func_fail, (name), (rc))

extern int c2_reqh_fop_init(void);
extern void c2_reqh_fop_fini(void);

bool c2_reqh_invariant(const struct c2_reqh *reqh)
{
	return reqh != NULL && reqh->rh_dbenv != NULL &&
		reqh->rh_cob_domain != NULL && reqh->rh_fol != NULL &&
		c2_fom_domain_invariant(&reqh->rh_fom_dom);
}

int  c2_reqh_init(struct c2_reqh *reqh, struct c2_dtm *dtm, struct c2_dbenv *db,
		  struct c2_cob_domain *cdom, struct c2_fol *fol)
{
	int result;

	C2_PRE(reqh != NULL);

	result = c2_fom_domain_init(&reqh->rh_fom_dom);
	if (result == 0) {
                reqh->rh_dtm = dtm;
                reqh->rh_dbenv = db;
                reqh->rh_cob_domain = cdom;
                reqh->rh_fol = fol;
		reqh->rh_shutdown = false;
                reqh->rh_fom_dom.fd_reqh = reqh;
		reqh->rh_addb = &reqh_addb_ctx;
                c2_reqh_svc_tlist_init(&reqh->rh_services);
                c2_reqh_rpc_mach_tlist_init(&reqh->rh_rpc_machines);
		c2_chan_init(&reqh->rh_sd_signal);
		c2_rwlock_init(&reqh->rh_rwlock);
		C2_POST(c2_reqh_invariant(reqh));
	} else
		REQH_ADDB_ADD(&reqh_addb_ctx, "c2_reqh_init", result);

	return result;
}

void c2_reqh_fini(struct c2_reqh *reqh)
{
        C2_PRE(reqh != NULL);
        c2_fom_domain_fini(&reqh->rh_fom_dom);
        c2_reqh_svc_tlist_fini(&reqh->rh_services);
        c2_reqh_rpc_mach_tlist_fini(&reqh->rh_rpc_machines);
	c2_rwlock_fini(&reqh->rh_rwlock);
}

void c2_reqhs_fini(void)
{
	c2_addb_ctx_fini(&reqh_addb_ctx);
	c2_reqh_service_types_fini();
	c2_reqh_fop_fini();
}

int c2_reqhs_init(void)
{
	c2_addb_ctx_init(&reqh_addb_ctx, &reqh_addb_ctx_type,
					&c2_addb_global_ctx);
	c2_reqh_service_types_init();
	c2_bob_type_tlist_init(&rqsvc_bob, &c2_reqh_svc_tl);
	c2_bob_type_tlist_init(&rqrpm_bob, &c2_reqh_rpc_mach_tl);
	return c2_reqh_fop_init();
}

void c2_reqh_fop_handle(struct c2_reqh *reqh,  struct c2_fop *fop)
{
	struct c2_fom	       *fom;
	int			result;
	bool                    rsd;

	C2_PRE(reqh != NULL);
	C2_PRE(fop != NULL);

	c2_rwlock_read_lock(&reqh->rh_rwlock);
	rsd = reqh->rh_shutdown;
	if (rsd) {
		REQH_ADDB_ADD(reqh->rh_addb, "c2_reqh_fop_handle",
                              ESHUTDOWN);
		c2_rwlock_read_unlock(&reqh->rh_rwlock);
		return;
	}

	C2_ASSERT(fop->f_type != NULL);
	C2_ASSERT(fop->f_type->ft_fom_type.ft_ops != NULL);
	C2_ASSERT(fop->f_type->ft_fom_type.ft_ops->fto_create != NULL);

	result = fop->f_type->ft_fom_type.ft_ops->fto_create(fop, &fom);

	if (result == 0)
		c2_fom_queue(fom, reqh);
	else
		REQH_ADDB_ADD(reqh->rh_addb, "c2_reqh_fop_handle", result);

	c2_rwlock_read_unlock(&reqh->rh_rwlock);
}

struct c2_reqh_service *c2_reqh_service_find_by_name(const char *sname,
						     struct c2_reqh *reqh)
{
	struct c2_reqh_service *service;
	
	c2_rwlock_read_lock(&reqh->rh_rwlock);
	c2_tl_for(c2_reqh_svc, &reqh->rh_services, service) {
		C2_ASSERT(c2_reqh_service_invariant(service));
		if (strcmp(service->rs_type->rst_name, sname) == 0)
			break;
	} c2_tl_endfor;
	c2_rwlock_read_unlock(&reqh->rh_rwlock);

	return service;
}

struct c2_reqh_service *c2_reqh_service_find(const struct c2_fom_type *ft,
					struct c2_reqh *reqh)
{
	C2_PRE(ft != NULL && reqh != NULL);

	return reqh->rh_key[ft->ft_rstype->rst_key];
}

void c2_reqh_shutdown_wait(struct c2_reqh *reqh)
{
	struct c2_clink clink;

        c2_rwlock_write_lock(&reqh->rh_rwlock);
        reqh->rh_shutdown = true;
        c2_rwlock_write_unlock(&reqh->rh_rwlock);

        c2_clink_init(&clink, NULL);
        c2_clink_add(&reqh->rh_sd_signal, &clink);

	while (c2_atomic64_get(&reqh->rh_fom_dom.fd_foms_nr) > 0)
		c2_chan_wait(&clink);

	c2_clink_del(&clink);
	c2_clink_fini(&clink);
}

static unsigned keymax = 0;

unsigned c2_reqh_key_init()
{
	C2_PRE(keymax < REQH_KEY_MAX - 1);
	return keymax++;
}

void *c2_reqh_key_find(struct c2_reqh *reqh, unsigned key, c2_bcount_t size)
{
	void **data;

	C2_PRE(IS_IN_ARRAY(key, reqh->rh_key) && reqh != NULL && size > 0);

	data = &reqh->rh_key[key];
	if (*data == NULL)
		C2_ALLOC_ADDB(*data, size, reqh->rh_addb, &reqh_addb_loc);
	return *data;
}

void c2_reqh_key_fini(struct c2_reqh *reqh, unsigned key)
{
	C2_PRE(IS_IN_ARRAY(key, reqh->rh_key));
	c2_free(reqh->rh_key[key]);
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
