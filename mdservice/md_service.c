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
 * Original author: Yuriy Umanets <yuriy_umanets@xyratex.com>
 * Original creation date: 23/07/2012
 */

/**
   @addtogroup mdservice
   @{
 */
#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#include "mdservice/mdservice_addb.h"

#include "lib/errno.h"
#include "lib/memory.h"
#include "mero/magic.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "layout/layout.h"
#include "mdservice/md_fops.h"
#include "mdservice/md_service.h"

static struct m0_addb_ctx m0_mds_mod_ctx;

static int mds_allocate(struct m0_reqh_service **service,
			struct m0_reqh_service_type *stype,
                        struct m0_reqh_context *rctx);
static void mds_fini(struct m0_reqh_service *service);

static int mds_start(struct m0_reqh_service *service);
static void mds_stop(struct m0_reqh_service *service);

/**
 * MD Service type operations.
 */
static const struct m0_reqh_service_type_ops mds_type_ops = {
        .rsto_service_allocate = mds_allocate
};

/**
 * MD Service operations.
 */
static const struct m0_reqh_service_ops mds_ops = {
        .rso_start = mds_start,
        .rso_stop  = mds_stop,
        .rso_fini  = mds_fini
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_mds_type, &mds_type_ops, "mdservice",
			     &m0_addb_ct_mds_serv);

M0_INTERNAL int m0_mds_register(void)
{
	m0_addb_ctx_type_register(&m0_addb_ct_mds_mod);
	m0_addb_ctx_type_register(&m0_addb_ct_mds_serv);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_mds_mod_ctx,
			 &m0_addb_ct_mds_mod, &m0_addb_proc_ctx);
        m0_reqh_service_type_register(&m0_mds_type);
        return m0_mdservice_fop_init();
}

M0_INTERNAL void m0_mds_unregister(void)
{
        m0_reqh_service_type_unregister(&m0_mds_type);
        m0_mdservice_fop_fini();
	m0_addb_ctx_fini(&m0_mds_mod_ctx);
}

/**
 * Allocates and initiates MD Service instance.
 * This operation allocates & initiates service instance with its operation
 * vector.
 */
static int mds_allocate(struct m0_reqh_service **service,
			struct m0_reqh_service_type *stype,
                        struct m0_reqh_context *rctx)
{
        struct m0_reqh_md_service *mds;

        M0_PRE(service != NULL && stype != NULL);

        M0_ALLOC_PTR_ADDB(mds, &m0_addb_gmc, M0_MDS_ADDB_LOC_ALLOCATE,
			  &m0_mds_mod_ctx);
        if (mds == NULL)
                return -ENOMEM;

        mds->rmds_magic = M0_MDS_REQH_SVC_MAGIC;

        *service = &mds->rmds_gen;
        (*service)->rs_ops = &mds_ops;
        return 0;
}

/**
 * Finalise MD Service instance.
 * This operation finalises service instance and de-allocate it.
 *
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static void mds_fini(struct m0_reqh_service *service)
{
        struct m0_reqh_md_service *serv_obj;

        M0_PRE(service != NULL);

        serv_obj = container_of(service, struct m0_reqh_md_service, rmds_gen);
        m0_free(serv_obj);
}

/**
 * Start MD Service.
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static int mds_start(struct m0_reqh_service *service)
{
        struct m0_reqh_md_service *serv_obj;
        int rc;
        M0_PRE(service != NULL);

        serv_obj = container_of(service, struct m0_reqh_md_service, rmds_gen);
        rc = m0_layout_domain_init(&serv_obj->rmds_layout_dom,
				   service->rs_reqh->rh_dbenv);
        if (rc == 0) {
                rc = m0_layout_standard_types_register(
					&serv_obj->rmds_layout_dom);
		if (rc != 0)
			m0_layout_domain_fini(&serv_obj->rmds_layout_dom);
	}

       return rc;
}

/**
 * Stops MD Service.
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static void mds_stop(struct m0_reqh_service *service)
{
        struct m0_reqh_md_service *serv_obj;
        M0_PRE(service != NULL);

        serv_obj = container_of(service, struct m0_reqh_md_service, rmds_gen);
        m0_layout_standard_types_unregister(&serv_obj->rmds_layout_dom);
        m0_layout_domain_fini(&serv_obj->rmds_layout_dom);
}

/** @} endgroup mdservice */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
