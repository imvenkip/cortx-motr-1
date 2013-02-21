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

#include "addb/addb.h"

#include "lib/errno.h"
#include "lib/memory.h"

#include "reqh/reqh_service.h"
#include "ut/cs_fop_foms.h"

static int ds1_service_start(struct m0_reqh_service *service);
static int ds2_service_start(struct m0_reqh_service *service);
static void ds1_service_stop(struct m0_reqh_service *service);
static void ds2_service_stop(struct m0_reqh_service *service);
static int ds1_service_allocate(struct m0_reqh_service **service,
				struct m0_reqh_service_type *stype,
				const char *arg);
static int ds2_service_allocate(struct m0_reqh_service **service,
				struct m0_reqh_service_type *stype,
				const char *arg);
static void ds_service_fini(struct m0_reqh_service *service);

static const struct m0_reqh_service_type_ops ds1_service_type_ops = {
        .rsto_service_allocate = ds1_service_allocate
};

static const struct m0_reqh_service_type_ops ds2_service_type_ops = {
        .rsto_service_allocate = ds2_service_allocate
};

static const struct m0_reqh_service_ops ds1_service_ops = {
        .rso_start = ds1_service_start,
        .rso_stop = ds1_service_stop,
        .rso_fini = ds_service_fini
};

static const struct m0_reqh_service_ops ds2_service_ops = {
        .rso_start = ds2_service_start,
        .rso_stop = ds2_service_stop,
        .rso_fini = ds_service_fini
};

M0_ADDB_CT(m0_addb_ct_ut_service, M0_ADDB_CTXID_UT_SERVICE, "hi", "low");
M0_REQH_SERVICE_TYPE_DEFINE(ds1_service_type, &ds1_service_type_ops, "ds1",
			     &m0_addb_ct_ut_service);
M0_REQH_SERVICE_TYPE_DEFINE(ds2_service_type, &ds2_service_type_ops, "ds2",
			     &m0_addb_ct_ut_service);

struct m0_reqh_service_type *m0_cs_default_stypes[] = {
	&ds1_service_type,
	&ds2_service_type,
};

const size_t m0_cs_default_stypes_nr = ARRAY_SIZE(m0_cs_default_stypes);

static int _ds_alloc(struct m0_reqh_service **service,
		     struct m0_reqh_service_type *stype,
		     const struct m0_reqh_service_ops *ops)
{
	struct m0_reqh_service *s;

	M0_PRE(stype != NULL && service != NULL && ops != NULL);

	M0_ALLOC_PTR(s);
	if (s == NULL)
		return -ENOMEM;

	s->rs_type = stype;
	s->rs_ops = ops;
	*service = s;

	return 0;
}

static int ds1_service_allocate(struct m0_reqh_service **service,
				struct m0_reqh_service_type *stype,
				const char *arg __attribute__((unused)))
{
	return _ds_alloc(service, stype, &ds1_service_ops);
}

static int ds2_service_allocate(struct m0_reqh_service **service,
				struct m0_reqh_service_type *stype,
				const char *arg __attribute__((unused)))
{
	return _ds_alloc(service, stype, &ds2_service_ops);
}

static int ds1_service_start(struct m0_reqh_service *service)
{
	int rc;

        M0_PRE(service != NULL);

        /*Initialise service fops.*/
	rc = m0_cs_ut_ds1_fop_init();
	M0_ASSERT(rc == 0);

        return rc;
}

static int ds2_service_start(struct m0_reqh_service *service)
{
        int rc;

        M0_PRE(service != NULL);

        /*Initialise service fops.*/
        rc = m0_cs_ut_ds2_fop_init();
	M0_ASSERT(rc == 0);

        return rc;
}

static void ds1_service_stop(struct m0_reqh_service *service)
{

        M0_PRE(service != NULL);

	/* Finalise service fops */
	m0_cs_ut_ds1_fop_fini();
}

static void ds2_service_stop(struct m0_reqh_service *service)
{

        M0_PRE(service != NULL);

        /* Finalise service fops */
        m0_cs_ut_ds2_fop_fini();
}

static void ds_service_fini(struct m0_reqh_service *service)
{
	M0_PRE(service != NULL);

        m0_free(service);
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
