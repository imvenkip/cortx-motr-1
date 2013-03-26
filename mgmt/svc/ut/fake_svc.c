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
 * Original creation date: 25-Mar-2013
 */

/* This file is designed to be included by mgmt/svc/ut/mgmt_svc_ut.c */

/*
 ******************************************************************************
 * Fake service
 ******************************************************************************
 */
struct mgmt_svc_ut_svc {
	struct m0_reqh_service msus_reqhs;
};
static struct mgmt_svc_ut_svc *mgmt_svc_ut_fake_svc;

static int mgmt_svc_ut_svc_rso_start(struct m0_reqh_service *service)
{
	M0_UT_ASSERT(service->rs_state == M0_RST_STARTING);
	return 0;
}

static void mgmt_svc_ut_svc_rso_prepare_to_stop(struct m0_reqh_service *service)
{
	M0_UT_ASSERT(service->rs_state == M0_RST_STARTED);
}

static void mgmt_svc_ut_svc_rso_stop(struct m0_reqh_service *service)
{
	M0_UT_ASSERT(service->rs_state == M0_RST_STOPPING);
}

static void mgmt_svc_ut_svc_rso_fini(struct m0_reqh_service *service)
{
	M0_UT_ASSERT(M0_IN(service->rs_state, (M0_RST_STOPPED, M0_RST_FAILED)));
	m0_free(service);
	mgmt_svc_ut_fake_svc = NULL;
}

static int mgmt_svc_ut_svc_rso_fop_accept_rc;
static int mgmt_svc_ut_svc_rso_fop_accept(struct m0_reqh_service *service,
					  struct m0_fop *fop)
{
	M0_UT_ASSERT(service->rs_state == M0_RST_STOPPING ||
		     service->rs_state == M0_RST_STARTED);
	M0_UT_ASSERT(m0_reqh_state_get(service->rs_reqh) == M0_REQH_ST_NORMAL ||
		     m0_reqh_state_get(service->rs_reqh) == M0_REQH_ST_DRAIN);
	return mgmt_svc_ut_svc_rso_fop_accept_rc;
}

static struct m0_reqh_service_ops mgmt_svc_ut_svc_ops = { /* not const */
	.rso_start           = mgmt_svc_ut_svc_rso_start,
	.rso_prepare_to_stop = mgmt_svc_ut_svc_rso_prepare_to_stop,
	.rso_stop            = mgmt_svc_ut_svc_rso_stop,
	.rso_fini            = mgmt_svc_ut_svc_rso_fini,
	.rso_fop_accept      = mgmt_svc_ut_svc_rso_fop_accept
};

static void mgmt_svc_ut_svc_restore_defaults(void)
{
	/* restore modifiable things */
	mgmt_svc_ut_svc_rso_fop_accept_rc = -ESHUTDOWN;
	mgmt_svc_ut_svc_ops.rso_fop_accept = mgmt_svc_ut_svc_rso_fop_accept;
}

static int mgmt_svc_ut_rsto_service_allocate(struct m0_reqh_service **service,
					     struct m0_reqh_service_type *stype,
				     const char *arg __attribute__((unused)))
{
	struct mgmt_svc_ut_svc *svc;

	M0_ALLOC_PTR(svc);
	if (svc == NULL) {
		return -ENOMEM;
	}
	*service = &svc->msus_reqhs;
	(*service)->rs_type = stype;
	(*service)->rs_ops = &mgmt_svc_ut_svc_ops;

	mgmt_svc_ut_fake_svc = svc;
	mgmt_svc_ut_svc_restore_defaults();

	return 0;
}

static struct m0_reqh_service_type_ops mgmt_svc_ut_svc_type_ops = {
	.rsto_service_allocate = mgmt_svc_ut_rsto_service_allocate,
};

#define M0_MGMT_SVC_UT_SVC_TYPE_NAME "mgmt-ut-svc"
M0_REQH_SERVICE_TYPE_DEFINE(m0_mgmt_svc_ut_svc_type, &mgmt_svc_ut_svc_type_ops,
                            M0_MGMT_SVC_UT_SVC_TYPE_NAME,
			    &m0_addb_ct_mgmt_service); /* reuse mgmt svc ct */

static int mgmt_svc_ut_svc_start(struct m0_reqh *rh)
{
	int                          rc;
	struct m0_reqh_service_type *svct;
	struct m0_reqh_service      *svc;

	svct = m0_reqh_service_type_find(M0_MGMT_SVC_UT_SVC_TYPE_NAME);
	M0_UT_ASSERT(svct != NULL);

	rc = m0_reqh_service_allocate(&svc, svct, NULL);
	if (rc != 0)
		return rc;
	m0_reqh_service_init(svc, rh);
	rc = m0_reqh_service_start(svc);
	if (rc != 0)
		m0_reqh_service_fini(svc);
	return rc;
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
