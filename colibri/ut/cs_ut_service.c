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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 12/10/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lib/ut.h"
#include "lib/errno.h"
#include "lib/memory.h"

#include "reqh/reqh_service.h"
#include "colibri/colibri_setup.h"

extern int cs_ds1_fop_init(void);
extern void cs_ds1_fop_fini(void);
extern int cs_ds2_fop_init(void);
extern void cs_ds2_fop_fini(void);

static int ds1_service_start(struct c2_reqh_service *service);
static int ds2_service_start(struct c2_reqh_service *service);
static int ds1_service_stop(struct c2_reqh_service *service);
static int ds2_service_stop(struct c2_reqh_service *service);
static int ds1_service_alloc_init(struct c2_reqh_service_type *stype,
                struct c2_reqh *reqh, struct c2_reqh_service **service);
static int ds2_service_alloc_init(struct c2_reqh_service_type *stype,
                struct c2_reqh *reqh, struct c2_reqh_service **service);
static void ds_service_fini(struct c2_reqh_service *service);

static const struct c2_reqh_service_type_ops ds1_service_type_ops = {
        .rsto_service_alloc_and_init = ds1_service_alloc_init
};

static const struct c2_reqh_service_type_ops ds2_service_type_ops = {
        .rsto_service_alloc_and_init = ds2_service_alloc_init
};

static const struct c2_reqh_service_ops ds1_service_ops = {
        .rso_start = ds1_service_start,
        .rso_stop = ds1_service_stop,
        .rso_fini = ds_service_fini
};

static const struct c2_reqh_service_ops ds2_service_ops = {
        .rso_start = ds2_service_start,
        .rso_stop = ds2_service_stop,
        .rso_fini = ds_service_fini
};

C2_REQH_SERVICE_TYPE_DECLARE(ds1_service_type, &ds1_service_type_ops, "ds1");
C2_REQH_SERVICE_TYPE_DECLARE(ds2_service_type, &ds2_service_type_ops, "ds2");

static int ds1_service_alloc_init(struct c2_reqh_service_type *stype,
                struct c2_reqh *reqh, struct c2_reqh_service **service)
{
        struct c2_reqh_service      *serv;
	int                          rc;

        C2_PRE(stype != NULL && reqh != NULL && service != NULL);

        C2_ALLOC_PTR(serv);
        if (serv == NULL)
                return -ENOMEM;

        serv->rs_type = stype;
        serv->rs_ops = &ds1_service_ops;

        rc = c2_reqh_service_init(serv, reqh);
	C2_UT_ASSERT(rc == 0);

        *service = serv;

        return 0;
}

static int ds2_service_alloc_init(struct c2_reqh_service_type *stype,
                struct c2_reqh *reqh, struct c2_reqh_service **service)
{
        struct c2_reqh_service      *serv;
	int                          rc;

        C2_PRE(stype != NULL && reqh != NULL && service != NULL);

        C2_ALLOC_PTR(serv);
        if (serv == NULL)
                return -ENOMEM;

        serv->rs_type = stype;
        serv->rs_ops = &ds2_service_ops;

        rc = c2_reqh_service_init(serv, reqh);
	C2_UT_ASSERT(rc == 0);

        *service = serv;

        return 0;
}

static int ds1_service_start(struct c2_reqh_service *service)
{
	int rc;

        C2_PRE(service != NULL);

        /*Initialise service fops.*/
	rc = cs_ds1_fop_init();
	C2_UT_ASSERT(rc == 0);

        rc = c2_reqh_service_start(service);
	C2_UT_ASSERT(rc == 0);

        return rc;
}

static int ds2_service_start(struct c2_reqh_service *service)
{
        int rc;

        C2_PRE(service != NULL);

        /*Initialise service fops.*/
        rc = cs_ds2_fop_init();
	C2_UT_ASSERT(rc == 0);

        rc = c2_reqh_service_start(service);
	C2_UT_ASSERT(rc == 0);

        return rc;
}

static int ds1_service_stop(struct c2_reqh_service *service)
{

        C2_PRE(service != NULL);

	/* Finalise service fops */
	cs_ds1_fop_fini();
        c2_reqh_service_stop(service);

        return 0;
}

static int ds2_service_stop(struct c2_reqh_service *service)
{

        C2_PRE(service != NULL);

        /* Finalise service fops */
        cs_ds2_fop_fini();
        c2_reqh_service_stop(service);

        return 0;
}

static void ds_service_fini(struct c2_reqh_service *service)
{
        c2_reqh_service_fini(service);
        c2_free(service);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
