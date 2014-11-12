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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 12/09/2012
 */

/**
   @page ADDB-DLD-SVC Service

   This design relates to the ADDB service. The ADDB service provides the
   following functionality:
   - Periodic posting of request handler related statistical data
   - Receipt of remote ADDB data (FUTURE)

   - @ref ADDB-DLD-SVC-fspec
   - @ref ADDB-DLD-SVC-lspec
     - @ref ADDB-DLD-SVC-pstats "Periodic Posting of Statistics"

   <hr>
   @section ADDB-DLD-SVC-fspec Functional Specification
   The primary data structures involved are:
   - addb_svc
   - addb_post_fom

   The interfaces involved are:
   - addb_pfom_mod_fini()
   - addb_pfom_mod_init()
   - addb_pfom_start()
   - m0_addb_svc_mod_fini()
   - m0_addb_svc_mod_init()

   <hr>
   @section ADDB-DLD-SVC-lspec Logical Specification
   The following subsections are present:
   - @subpage ADDB-DLD-SVC-pstats "Periodic Posting of Statistics"

 */

/* This file is designed to be included by addb/addb.c */

#include "addb/addb_svc.h"

/**
   @ingroup addb_svc_pvt
   @{
 */

static void addb_pfom_mod_fini(void);
static int  addb_pfom_mod_init(void);
static void addb_pfom_start(struct addb_svc *svc);

static const struct m0_bob_type addb_svc_bob = {
	.bt_name = "addb svc",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct addb_svc, as_magic),
	.bt_magix = M0_ADDB_SVC_MAGIC,
	.bt_check = NULL
};

M0_BOB_DEFINE(static, &addb_svc_bob, addb_svc);

/**
   UT handle to a started singleton service.  Every instance started will
   overwrite this without serialization.
 */
static struct addb_svc *the_addb_svc;

/*
 ******************************************************************************
 * ADDB service
 ******************************************************************************
 */
static bool addb_svc_invariant(const struct addb_svc *svc)
{
	return addb_svc_bob_check(svc);
}

/**
   The rso_start method to start the ADDB service and launch startup time
   FOMs.
 */
static int addb_svc_rso_start(struct m0_reqh_service *service)
{
	struct addb_svc *svc;

	M0_LOG(M0_DEBUG, "starting");
	M0_PRE(m0_reqh_service_state_get(service) == M0_RST_STARTING);

	svc = bob_of(service, struct addb_svc, as_reqhs, &addb_svc_bob);
	if (!M0_FI_ENABLED("skip_pfom_start"))
		addb_pfom_start(svc);
	the_addb_svc = svc;
	return 0;
}

/**
   The rso_prepare_to_stop method terminates the persistent FOMs.
 */
static void addb_svc_rso_prepare_to_stop(struct m0_reqh_service *service)
{
	struct addb_svc *svc;

	M0_LOG(M0_DEBUG, "preparing to stop");
	M0_PRE(m0_reqh_service_state_get(service) == M0_RST_STOPPING);
	svc = bob_of(service, struct addb_svc, as_reqhs, &addb_svc_bob);
	if (!M0_FI_ENABLED("skip_pfom_stop"))
		m0_fom_wakeup(&svc->as_pfom.pf_fom);
}

/**
   The rso_stop method to stop the ADDB service.
 */
static void addb_svc_rso_stop(struct m0_reqh_service *service)
{
	M0_LOG(M0_DEBUG, "stopping");
	M0_PRE(m0_reqh_service_state_get(service) == M0_RST_STOPPED);
}

/**
   The rso_fini method to finalize the ADDB service.
 */
static void addb_svc_rso_fini(struct m0_reqh_service *service)
{
	struct addb_svc *svc;

	M0_LOG(M0_DEBUG, "done");
	M0_PRE(M0_IN(m0_reqh_service_state_get(service), (M0_RST_STOPPED,
	                                                  M0_RST_FAILED)));
	svc = bob_of(service, struct addb_svc, as_reqhs, &addb_svc_bob);
	m0_cond_fini(&svc->as_cond);
	addb_svc_bob_fini(svc);
	the_addb_svc = NULL;
	m0_free(svc);
}

static const struct m0_reqh_service_ops addb_service_ops = {
	.rso_start           = addb_svc_rso_start,
	.rso_start_async     = m0_reqh_service_async_start_simple,
	.rso_prepare_to_stop = addb_svc_rso_prepare_to_stop,
	.rso_stop            = addb_svc_rso_stop,
	.rso_fini            = addb_svc_rso_fini
};

/*
 ******************************************************************************
 * ADDB service type
 ******************************************************************************
 */

/**
   The rsto_service_allocate method to allocate an ADDB service instance.
 */
static int
addb_svc_rsto_service_allocate(struct m0_reqh_service **service,
			       const struct m0_reqh_service_type *stype)
{
	struct addb_svc *svc;

	M0_ALLOC_PTR(svc);
	if (svc == NULL) {
		M0_LOG(M0_ERROR, "Unable to allocate memory for ADDB service");
		return M0_ERR(-ENOMEM);
	}
	*service = &svc->as_reqhs;
	(*service)->rs_type = stype;
	(*service)->rs_ops = &addb_service_ops;
	m0_cond_init(&svc->as_cond, &(*service)->rs_mutex);
	addb_svc_bob_init(svc);

	M0_POST(addb_svc_invariant(svc));
	return 0;
}

static const struct m0_reqh_service_type_ops addb_service_type_ops = {
	.rsto_service_allocate = addb_svc_rsto_service_allocate,
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_addb_svc_type, &addb_service_type_ops,
                            M0_ADDB_SVC_NAME, &m0_addb_ct_addb_service, 2);

/** @} end group addb_svc_pvt */

/*
 ******************************************************************************
 * Public interfaces
 ******************************************************************************
 */

M0_INTERNAL int m0_addb_svc_mod_init(void)
{
	int rc;
	rc = m0_reqh_service_type_register(&m0_addb_svc_type);
	if (rc == 0) {
		rc = addb_pfom_mod_init();
		if (rc != 0)
			m0_reqh_service_type_unregister(&m0_addb_svc_type);
	}
	return M0_RC(rc);
}

M0_INTERNAL void m0_addb_svc_mod_fini(void)
{
	addb_pfom_mod_fini();
        m0_reqh_service_type_unregister(&m0_addb_svc_type);
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
