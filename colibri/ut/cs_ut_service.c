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

#include "lib/errno.h"
#include "lib/memory.h"

#include "reqh/reqh_service.h"
#include "colibri/colibri_setup.h"

/**
   @addtogroup colibri_setup
   @{
 */

extern int cs_fop_init(void);
extern void cs_fop_fini(void);

static int dummy_service_start(struct c2_reqh_service *service);
static int dummy_service_stop(struct c2_reqh_service *service);
static int dummy_service_alloc_init(struct c2_reqh_service_type *stype,
                struct c2_reqh *reqh, struct c2_reqh_service **service);
static void dummy_service_fini(struct c2_reqh_service *service);

static const struct c2_reqh_service_type_ops dummy_service_type_ops = {
        .rsto_service_alloc_and_init = dummy_service_alloc_init
};

static const struct c2_reqh_service_ops dummy_service_ops = {
        .rso_start = dummy_service_start,
        .rso_stop = dummy_service_stop,
        .rso_fini = dummy_service_fini
};

C2_REQH_SERVICE_TYPE_DECLARE(dummy_service_type, &dummy_service_type_ops, "dummy");

static int dummy_service_alloc_init(struct c2_reqh_service_type *stype,
                struct c2_reqh *reqh, struct c2_reqh_service **service)
{
        struct c2_reqh_service      *serv;

        C2_PRE(service != NULL && stype != NULL);

        C2_ALLOC_PTR(serv);
        if (serv == NULL)
                return -ENOMEM;

        serv->rs_type = stype;
        serv->rs_ops = &dummy_service_ops;

        c2_reqh_service_init(serv, reqh);
        *service = serv;

        return 0;
}

static int dummy_service_start(struct c2_reqh_service *service)
{
	int rc;

        C2_PRE(service != NULL);

        /*Initialise service fops.*/
	rc = cs_fop_init();
	if (rc != 0)
		goto out;

        c2_reqh_service_start(service);

out:
        return rc;
}

static int dummy_service_stop(struct c2_reqh_service *service)
{

        C2_PRE(service != NULL);

	/* Finalise service fops */
	cs_fop_fini();
        c2_reqh_service_stop(service);

        return 0;
}

static void dummy_service_fini(struct c2_reqh_service *service)
{
        c2_reqh_service_fini(service);
        c2_free(service);
}

/** @} endgroup colibri_setup */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
