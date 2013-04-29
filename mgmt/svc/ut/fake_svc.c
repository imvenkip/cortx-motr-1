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
	bool                   msus_used_timer;
	bool                   msus_timer_ticked;
	struct m0_timer        msus_timer; /* for start async */
};
static struct mgmt_svc_ut_svc *mgmt_svc_ut_fake_svc;

static int mgmt_svc_ut_start_rc;
static int mgmt_svc_ut_rso_start_called;
static int mgmt_svc_ut_svc_rso_start(struct m0_reqh_service *service)
{
	M0_UT_ASSERT(m0_reqh_service_state_get(service) == M0_RST_STARTING);
	++mgmt_svc_ut_rso_start_called;
	return mgmt_svc_ut_start_rc;
}

static int mgmt_svc_ut_start_async_result;
static int mgmt_svc_ut_start_timer_called;
static unsigned long mgmt_svc_ut_timer_callback(unsigned long data)
{
	struct m0_reqh_service_start_async_ctx *asc;
	struct mgmt_svc_ut_svc *svc;

	M0_ENTRY();
	asc = (struct m0_reqh_service_start_async_ctx *)data;
	svc = (struct mgmt_svc_ut_svc *)asc->sac_service;
	if (svc->msus_timer_ticked) {
		M0_LOG(M0_DEBUG, "timer already ticked");
		M0_RETURN(0);
	}
	svc->msus_timer_ticked = true;
	asc->sac_rc = mgmt_svc_ut_start_async_result;
	m0_fom_wakeup(asc->sac_fom);
	++mgmt_svc_ut_start_timer_called;
	M0_RETURN(0);
}

static int mgmt_svc_ut_start_async_rc;
static int mgmt_svc_ut_rso_start_async_called;
static int mgmt_svc_ut_rso_start_async(struct m0_reqh_service_start_async_ctx
				       *asc)
{
	struct mgmt_svc_ut_svc *svc = container_of(asc->sac_service,
						   struct mgmt_svc_ut_svc,
						   msus_reqhs);

	M0_ENTRY();
	M0_UT_ASSERT(m0_reqh_service_state_get(asc->sac_service)
		     == M0_RST_STARTING);
	M0_UT_ASSERT(asc->sac_service == &mgmt_svc_ut_fake_svc->msus_reqhs);
	++mgmt_svc_ut_rso_start_async_called;
	if (mgmt_svc_ut_start_async_rc != 0)
		M0_RETURN(mgmt_svc_ut_start_async_rc);
	m0_timer_init(&svc->msus_timer, M0_TIMER_HARD,
		      m0_time_from_now(0, 5000000), /* 5ms */
		      mgmt_svc_ut_timer_callback, (unsigned long)asc);
	m0_timer_start(&svc->msus_timer);
	svc->msus_used_timer = true;
	M0_RETURN(0);
}

static void mgmt_svc_ut_svc_rso_prepare_to_stop(struct m0_reqh_service *service)
{
	M0_UT_ASSERT(m0_reqh_service_state_get(service) == M0_RST_STOPPING);
}

static void mgmt_svc_ut_svc_rso_stop(struct m0_reqh_service *service)
{
	M0_UT_ASSERT(m0_reqh_service_state_get(service) == M0_RST_STOPPED);
}

static int mgmt_svc_ut_rso_fini_called;
static void mgmt_svc_ut_svc_rso_fini(struct m0_reqh_service *service)
{
	struct mgmt_svc_ut_svc *svc = container_of(service,
						   struct mgmt_svc_ut_svc,
						   msus_reqhs);

	M0_ENTRY();
	M0_UT_ASSERT(M0_IN(m0_reqh_service_state_get(service),
	                   (M0_RST_STOPPED, M0_RST_FAILED)));
	++mgmt_svc_ut_rso_fini_called;
	if (svc->msus_used_timer) {
		m0_timer_stop(&svc->msus_timer);
		M0_UT_ASSERT(m0_timer_fini(&svc->msus_timer) == 0);
	}
	m0_free(service);
	mgmt_svc_ut_fake_svc = NULL;
}

static int mgmt_svc_ut_svc_rso_fop_accept_rc;
static int mgmt_svc_ut_svc_rso_fop_accept(struct m0_reqh_service *service,
					  struct m0_fop *fop)
{
	M0_UT_ASSERT(M0_IN(m0_reqh_service_state_get(service),
	                   (M0_RST_STOPPING, M0_RST_STARTED)));
	M0_UT_ASSERT(m0_reqh_state_get(service->rs_reqh) == M0_REQH_ST_NORMAL ||
		     m0_reqh_state_get(service->rs_reqh) == M0_REQH_ST_DRAIN);
	return mgmt_svc_ut_svc_rso_fop_accept_rc;
}

static struct m0_reqh_service_ops mgmt_svc_ut_svc_ops = { /* not const */
	.rso_start           = mgmt_svc_ut_svc_rso_start,
	.rso_start_async     = mgmt_svc_ut_rso_start_async,
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

static void mgmt_svc_ut_svc_restore_defaults_async(void)
{
	mgmt_svc_ut_svc_restore_defaults();
	mgmt_svc_ut_svc_ops.rso_start_async = mgmt_svc_ut_rso_start_async;
	mgmt_svc_ut_start_async_rc = 0;
	mgmt_svc_ut_start_async_result = 0;
	mgmt_svc_ut_start_rc = 0;
	mgmt_svc_ut_rso_start_called = 0;
	mgmt_svc_ut_rso_fini_called = 0;
	mgmt_svc_ut_rso_start_async_called = 0;
	mgmt_svc_ut_start_timer_called = 0;
}

static int mgmt_svc_ut_rsto_service_allocate(struct m0_reqh_service **service,
					     struct m0_reqh_service_type *stype,
					     struct m0_reqh_context *rctx)
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
#define M0_MGMT_SVC_UT_SVC_UUID "abcdef01-2345-6789-abcd-ef0123456789"
M0_REQH_SERVICE_TYPE_DEFINE(m0_mgmt_svc_ut_svc_type, &mgmt_svc_ut_svc_type_ops,
                            M0_MGMT_SVC_UT_SVC_TYPE_NAME,
			    &m0_addb_ct_mgmt_service); /* reuse mgmt svc ct */

static int mgmt_svc_ut_svc_start(struct m0_reqh *rh, bool async)
{
	int                          rc;
	struct m0_reqh_service_type *svct;
	struct m0_reqh_service      *svc;
	struct m0_uint128            uuid;

	svct = m0_reqh_service_type_find(M0_MGMT_SVC_UT_SVC_TYPE_NAME);
	M0_UT_ASSERT(svct != NULL);

	if (!async)
		mgmt_svc_ut_svc_restore_defaults_async();
	rc = m0_reqh_service_allocate(&svc, svct, NULL);
	if (rc != 0)
		return rc;
	m0_uuid_parse(M0_MGMT_SVC_UT_SVC_UUID, &uuid);
	m0_reqh_service_init(svc, rh, &uuid);
	if (async) {
		rc = m0_mgmt_reqh_service_start(svc);
	} else {
		rc = m0_reqh_service_start(svc);
		if (rc != 0)
			m0_reqh_service_fini(svc);
	}
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
