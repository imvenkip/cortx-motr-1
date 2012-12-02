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

#include "lib/errno.h"
#include "lib/memory.h"
#include "colibri/magic.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "mdservice/md_service.h"
#include "mdservice/md_fops.h"

/* ADDB context for mds. */
static struct c2_addb_ctx mds_addb_ctx;

/* ADDB location for mds. */
static const struct c2_addb_loc mds_addb_loc = {
        .al_name = "md_service",
};

/* ADDB context type for mds. */
static const struct c2_addb_ctx_type mds_addb_ctx_type = {
        .act_name = "md_service",
};

static int mds_allocate(struct c2_reqh_service_type *stype,
                        struct c2_reqh_service **service);
static void mds_fini(struct c2_reqh_service *service);

static int mds_start(struct c2_reqh_service *service);
static void mds_stop(struct c2_reqh_service *service);

/**
 * MD Service type operations.
 */
static const struct c2_reqh_service_type_ops mds_type_ops = {
        .rsto_service_allocate = mds_allocate
};

/**
 * MD Service operations.
 */
static const struct c2_reqh_service_ops mds_ops = {
        .rso_start = mds_start,
        .rso_stop  = mds_stop,
        .rso_fini  = mds_fini
};

C2_REQH_SERVICE_TYPE_DEFINE(c2_mds_type, &mds_type_ops, "mdservice");

C2_INTERNAL int c2_mds_register(void)
{
        c2_reqh_service_type_register(&c2_mds_type);
        return c2_mdservice_fop_init();
}

C2_INTERNAL void c2_mds_unregister(void)
{
        c2_reqh_service_type_unregister(&c2_mds_type);
        c2_mdservice_fop_fini();
}

/**
 * Allocates and initiates MD Service instance.
 * This operation allocates & initiates service instance with its operation
 * vector.
 *
 * @param stype service type
 * @param service pointer to service instance.
 *
 * @pre stype != NULL && service != NULL
 */
static int mds_allocate(struct c2_reqh_service_type *stype,
                        struct c2_reqh_service **service)
{
        struct c2_reqh_service    *serv;
        struct c2_reqh_md_service *serv_obj;

        C2_PRE(stype != NULL && service != NULL);

        c2_addb_ctx_init(&mds_addb_ctx, &mds_addb_ctx_type,
                         &c2_addb_global_ctx);

        C2_ALLOC_PTR_ADDB(serv_obj, &mds_addb_ctx, &mds_addb_loc);
        if (serv_obj == NULL)
                return -ENOMEM;

        serv_obj->rmds_magic = C2_MDS_REQH_SVC_MAGIC;
        serv = &serv_obj->rmds_gen;

        serv->rs_type = stype;
        serv->rs_ops = &mds_ops;
        *service = serv;

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
static void mds_fini(struct c2_reqh_service *service)
{
        struct c2_reqh_md_service *serv_obj;

        C2_PRE(service != NULL);

        c2_addb_ctx_fini(&mds_addb_ctx);

        serv_obj = container_of(service, struct c2_reqh_md_service, rmds_gen);
        c2_free(serv_obj);
}

/**
 * Start MD Service.
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static int mds_start(struct c2_reqh_service *service)
{
        int rc = 0;
        C2_PRE(service != NULL);
        return rc;
}

/**
 * Stops MD Service.
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static void mds_stop(struct c2_reqh_service *service)
{
        C2_PRE(service != NULL);
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
