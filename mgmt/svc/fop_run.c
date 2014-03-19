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
 * Original creation date: 10-Apr-2013
 */

/**
   @page MGMT-SVC-DLD-FOP-SR Management Service Run FOP
   This FOP is used to start a @ref reqhservice "Request Handler Service"
   in an orderly manner that permits potentially blocking activities
   required to start the service.
   @todo Currently the FOP is not supported but its FOM is implemented.

   The FOM can be launched internally with the m0_mgmt_reqh_service_start()
   subroutine; this is used during m0d startup by the m0_cs_start() subroutine.
   If the request handler is in or transitions to the ::M0_REQH_ST_NORMAL state
   at this time, then starting services can communicate with services that have
   already started, which permits non-cyclic startup dependencies between
   services.

   Introduced to support this FOM is a new service operation
   m0_reqh_service_ops::rso_start_async().
@code
struct m0_reqh_service_start_async_ctx {
	struct m0_reqh_service *sac_service;
	struct m0_fom          *sac_fom;
	int                     sac_rc;
};

struct m0_reqh_service_ops {
	int (*rso_start_async)(struct m0_reqh_service_start_async_ctx *asc);
	...
}
@endcode
   The startup FOM transitions services to the ::M0_RST_STARTING state by
   calling m0_reqh_service_start_async(), which in turn, invokes the new
   rso_start_async() service operation.  The m0_reqh_service_started()
   subroutine must be called to finally transition the service to the
   ::M0_RST_STARTED state.
   See @ref reqhservice_state_dia "Request Handler Service State Diagram" for
   details.

   A service should use this new operation method to launch internal FOMs to
   perform any potentially blocking activities necessary for startup.  The
   startup FOM always blocks on successful return from the rso_start_async()
   operation method.  The FOM must be awoken by an invocation of the
   m0_fom_wakeup() subroutine - this is expected to be done by the background
   service specific logic that is doing the initialization.  The pointer to the
   FOM object is provided in the sac_fom field of the context argument passed to
   the operation for this purpose, along with the sac_rc field that carries back
   the result of the asynchronous startup activity.  The call to m0_fom_wakeup()
   can be made from within the body of the operation method or from elsewhere.

   The FOM also provides backward compatability to the older synchronous
   m0_reqh_service_ops::rso_start() method, invoked by a call to
   m0_reqh_service_start().  It wraps the call with m0_fom_block_enter() and
   m0_fom_block_leave() just in case any blocking operations are performed.
   The newer rso_start_async() operation will be used in preference to the older
   rso_start() method if both are present.

   The FOM has the following simple phase state machine:
   @dot
   digraph run_fom_phases {
       CALL_A [label="CALL_A\nrso_start_async()"]
       CALL_S [label="CALL_S\nrso_start()"]
       INIT      -> CALL_A
       INIT      -> CALL_S
       CALL_A    -> CALL_A_WAIT
       CALL_A    -> FINI
       CALL_A_WAIT  -> FINI [label="m0_fom_wakeup()"]
       CALL_S    -> FINI
   }
   @enddot
   The states are:
   - @b INIT Initial state.
   - @b CALL_A The FOM invokes the rso_start_async() operation to start the
        service and transitions to CALL_A_WAIT state.
   - @b CALL_A_WAIT Sleep at this state until the service logic invokes
        m0_fom_wakeup() on sac_fom. Set the state of the service based on
        the asynchronous operation result set in sac_rc.
   - @b CALL_S The FOM invokes the rso_start() operation to start the service.
        The state of the service is determined from the result of the method.
   - @b FINI Final state

   @todo Add states to support the FOP.

   The management service will keep track of active RUN foms in the
   mgmt_svc::ms_run_foms counter.  This is used only during startup and is
   defined as an atomic variable so that it can be read from different threads
   without cache penalties.  Decrements to this variable must be protected with
   the request handler rh_sm_grp lock and accompanied by a broadcast on the
   group channel.  This provides a blocking context for a thread that wants
   to wait on starting services.
   The m0_mgmt_reqh_services_start_wait() subroutine is provided with this logic
   to block waiting for service startup to complete.

   @see mgmt_fop_run_fom,
        m0_mgmt_reqh_service_start(), m0_mgmt_reqh_services_start_wait(),
        m0_reqh_service_start(), m0_reqh_service_start_async(),
        m0_reqh_service_started(),
        @ref reqhservice_state_dia "Request Handler Service State Diagram"
 */


/* This file is designed to be included by mgmt/mgmt.c */

#ifdef M0_MGMT_SERVICE_PRESENT
/**
   @addtogroup mgmt_svc_pvt
   @{
 */

/*
 ******************************************************************************
 * FOM
 ******************************************************************************
 */

/**
   Internal representation of the run FOM
 */
struct mgmt_fop_run_fom {
	uint64_t                               sf_magic;
	struct m0_reqh_service_start_async_ctx sf_ctx;
	struct m0_fom                          sf_m0fom;
};

static const struct m0_bob_type mgmt_fop_run_fom_bob = {
	.bt_name         = "mgmt run fom",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct mgmt_fop_run_fom, sf_magic),
	.bt_magix        = M0_MGMT_FOP_RUN_FOM_MAGIC,
	.bt_check        = NULL
};

M0_BOB_DEFINE(static, &mgmt_fop_run_fom_bob, mgmt_fop_run_fom);

/*
 ******************************************************************************
 * FOM Phase state machine
 ******************************************************************************
 */
enum mgmt_fop_run_phases {
	MGMT_FOP_RUN_PHASE_INIT      = M0_FOM_PHASE_INIT,
	MGMT_FOP_RUN_PHASE_FINI      = M0_FOM_PHASE_FINISH,
	MGMT_FOP_RUN_PHASE_CALL_A    = M0_FOM_PHASE_NR,
	MGMT_FOP_RUN_PHASE_CALL_S,
	MGMT_FOP_RUN_PHASE_CALL_A_WAIT,

	MGMT_FOP_RUN_PHASE_NR
};

static struct m0_sm_state_descr mgmt_fop_run_descr[] = {
        [MGMT_FOP_RUN_PHASE_INIT] = {
                .sd_flags       = M0_SDF_INITIAL,
                .sd_name        = "Init",
                .sd_allowed     = M0_BITS(MGMT_FOP_RUN_PHASE_CALL_A,
					  MGMT_FOP_RUN_PHASE_CALL_S),
        },
        [MGMT_FOP_RUN_PHASE_CALL_A] = {
                .sd_flags       = 0,
                .sd_name        = "Call Async",
                .sd_allowed     = M0_BITS(MGMT_FOP_RUN_PHASE_CALL_A_WAIT,
					  MGMT_FOP_RUN_PHASE_FINI),
        },
        [MGMT_FOP_RUN_PHASE_CALL_A_WAIT] = {
                .sd_flags       = 0,
                .sd_name        = "Return Async",
                .sd_allowed     = M0_BITS(MGMT_FOP_RUN_PHASE_FINI)
        },
        [MGMT_FOP_RUN_PHASE_CALL_S] = {
                .sd_flags       = 0,
                .sd_name        = "Call Sync",
                .sd_allowed     = M0_BITS(MGMT_FOP_RUN_PHASE_FINI),
        },
        [MGMT_FOP_RUN_PHASE_FINI] = {
                .sd_flags       = M0_SDF_TERMINAL,
                .sd_name        = "Fini",
                .sd_allowed     = 0,
        },
};

static struct m0_sm_conf mgmt_fop_run_sm = {
	.scf_name      = "Service Run FOM Phases",
	.scf_nr_states = ARRAY_SIZE(mgmt_fop_run_descr),
	.scf_state     = mgmt_fop_run_descr,
};

/*
 ******************************************************************************
 * FOM methods
 ******************************************************************************
 */
static void mgmt_fop_run_fo_fini(struct m0_fom *fom)
{
        struct mgmt_fop_run_fom *sffom = bob_of(fom, struct mgmt_fop_run_fom,
					       sf_m0fom, &mgmt_fop_run_fom_bob);
	struct mgmt_svc         *mgmt_svc = bob_of(fom->fo_service,
						   struct mgmt_svc, ms_reqhs,
						   &mgmt_svc_bob);
	struct m0_reqh          *reqh = fom->fo_service->rs_reqh;

	M0_ENTRY();

	mgmt_fop_run_fom_bob_fini(sffom);
	m0_fom_fini(fom);
	m0_free(sffom);

	/* decrement count and notify waiters */
	m0_sm_group_lock(&reqh->rh_sm_grp);
	m0_atomic64_dec(&mgmt_svc->ms_run_foms);
	m0_chan_broadcast(&reqh->rh_sm_grp.s_chan);
	m0_sm_group_unlock(&reqh->rh_sm_grp);

	M0_LEAVE();
}

static size_t mgmt_fop_run_fo_locality(const struct m0_fom *fom)
{
	static size_t counter;

	M0_PRE(mgmt_fop_run_fom_bob_check(container_of(fom,
						       struct mgmt_fop_run_fom,
						       sf_m0fom)));

	return ++counter; /* no serialization required */
}

static int mgmt_fop_run_fo_tick(struct m0_fom *fom)
{
	int                      rc;
        struct mgmt_fop_run_fom *sffom = bob_of(fom, struct mgmt_fop_run_fom,
					       sf_m0fom, &mgmt_fop_run_fom_bob);
	struct m0_reqh_service  *service = sffom->sf_ctx.sac_service;

	M0_ENTRY();
	M0_ASSERT(m0_fom_phase(fom) < MGMT_FOP_RUN_PHASE_NR);
	M0_LOG(M0_DEBUG, "State: %s", fom->fo_sm_phase.sm_conf->
	       scf_state[fom->fo_sm_phase.sm_state].sd_name);

	rc = M0_FSO_AGAIN;
	switch (m0_fom_phase(fom)) {
	case MGMT_FOP_RUN_PHASE_INIT:
		if (service->rs_ops->rso_start_async != NULL)
			m0_fom_phase_set(fom, MGMT_FOP_RUN_PHASE_CALL_A);
		else
			m0_fom_phase_set(fom, MGMT_FOP_RUN_PHASE_CALL_S);
		break;

	case MGMT_FOP_RUN_PHASE_CALL_A:
		rc = m0_reqh_service_start_async(&sffom->sf_ctx);
		if (rc == 0) {
			m0_fom_phase_set(fom, MGMT_FOP_RUN_PHASE_CALL_A_WAIT);
		} else {
			M0_LOG(M0_DEBUG, "rso_start_async() rc = %d", rc);
			M0_ASSERT(m0_reqh_service_state_get(service)
				  == M0_RST_FAILED);
			m0_fom_phase_set(fom, MGMT_FOP_RUN_PHASE_FINI);
		}
		rc = M0_FSO_WAIT;
		break;

	case MGMT_FOP_RUN_PHASE_CALL_A_WAIT:
		M0_LOG(M0_DEBUG, "rso_start_async() result = %d",
		       sffom->sf_ctx.sac_rc);
		if (sffom->sf_ctx.sac_rc == 0)
			m0_reqh_service_started(service);
		else
			m0_reqh_service_failed(service);
		m0_fom_phase_set(fom, MGMT_FOP_RUN_PHASE_FINI);
		rc = M0_FSO_WAIT;
		break;

	case MGMT_FOP_RUN_PHASE_CALL_S:
		m0_fom_block_enter(fom);
		rc = m0_reqh_service_start(sffom->sf_ctx.sac_service);
		M0_LOG(M0_DEBUG, "rso_start() result = %d", rc);
		m0_fom_block_leave(fom);
		m0_fom_phase_set(fom, MGMT_FOP_RUN_PHASE_FINI);
		rc = M0_FSO_WAIT;
		break;

	default:
		M0_ASSERT(m0_fom_phase(fom) < MGMT_FOP_RUN_PHASE_FINI);
	}

	return M0_RC(rc);
}

static void mgmt_fop_run_fo_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	struct m0_reqh_service  *rsvc = fom->fo_service;
        struct mgmt_fop_run_fom *sffom = bob_of(fom, struct mgmt_fop_run_fom,
					       sf_m0fom, &mgmt_fop_run_fom_bob);

	M0_ADDB_CTX_INIT(mc, &fom->fo_addb_ctx, &m0_addb_ct_mgmt_fom_run,
			 &rsvc->rs_addb_ctx,
			 sffom->sf_ctx.sac_service->rs_service_uuid.u_hi,
			 sffom->sf_ctx.sac_service->rs_service_uuid.u_lo);
}

static const struct m0_fom_ops mgmt_fop_run_fom_ops = {
        .fo_fini          = mgmt_fop_run_fo_fini,
        .fo_tick          = mgmt_fop_run_fo_tick,
        .fo_home_locality = mgmt_fop_run_fo_locality,
	.fo_addb_init     = mgmt_fop_run_fo_addb_init
};

/*
 ******************************************************************************
 * FOM type ops
 ******************************************************************************
 */
static int mgmt_run_fom_create(struct m0_reqh_service *service,
			       struct m0_fom_type *fom_type,
			       const struct m0_fom_ops *fom_ops,
			       struct m0_fom **out)
{
	int                      rc;
	struct mgmt_fop_run_fom *sffom = NULL;
	struct mgmt_svc         *mgmt_svc;

	M0_PRE(service->rs_reqh != NULL);
	M0_PRE(m0_reqh_service_invariant(service));
	M0_PRE(m0_reqh_service_state_get(service) == M0_RST_INITIALISED);
	M0_PRE(service->rs_reqh->rh_mgmt_svc != NULL);
	mgmt_svc = bob_of(service->rs_reqh->rh_mgmt_svc, struct mgmt_svc,
			  ms_reqhs, &mgmt_svc_bob);
	M0_PRE(out != NULL);

	if (M0_FI_ENABLED("-ECANCELED"))
		return M0_RC(-ECANCELED);

	rc = -ENOMEM;
	MGMT_ALLOC_PTR(sffom, FOP_RUN_FTOC_1);
	if (sffom == NULL)
		goto failed;

	sffom->sf_ctx.sac_service = service;
	sffom->sf_ctx.sac_fom = &sffom->sf_m0fom;

	/* bless now as m0_fom_init() will invoke the fo_addb_init() method */
	mgmt_fop_run_fom_bob_init(sffom);

	m0_fom_init(&sffom->sf_m0fom, fom_type, fom_ops, NULL, NULL,
		    service->rs_reqh, service->rs_reqh->rh_mgmt_svc->rs_type);

	/* track this FOM */
	m0_atomic64_inc(&mgmt_svc->ms_run_foms);

	*out = &sffom->sf_m0fom;
	return M0_RC(0);

 failed:
	return M0_RC(rc);
}

/** @todo support this method when run FOP supported */
static int mgmt_fop_run_fto_create(struct m0_fop *fop, struct m0_fom **out,
				   struct m0_reqh *reqh)
{
	M0_ENTRY();

	M0_PRE(fop == NULL);
	M0_PRE(out != NULL);
	M0_PRE(reqh != NULL);

	/*
	 * Call mgmt_run_fom_create(), etc.
	 * Note that failed services are left in the reqh queue, but there
	 * is no existing support to restart them - something the future
	 * fop could do. It is beneficial for the future FOP to have them on the
	 * queue, because the FOP refers to services by their service UUID.
	 * The future fop may also want to see initialized but unstarted
	 * services.
	 */

	return -ENOSYS;
}

static const struct m0_fom_type_ops mgmt_fop_run_fom_type_ops = {
        .fto_create   = mgmt_fop_run_fto_create
};

/** FOM type - not needed once a FOP type is defined. */
static struct m0_fom_type mgmt_run_fom_type;

/** @} end group mgmt_svc_pvt */
#endif /* M0_MGMT_SERVICE_PRESENT */

/**
   @addtogroup mgmt_pvt
   @{
 */

/*
 ******************************************************************************
 * FOP initialization logic
 ******************************************************************************
 */

static int mgmt_fop_run_init(void)
{
	m0_addb_ctx_type_register(&m0_addb_ct_mgmt_fom_run);
#ifdef M0_MGMT_SERVICE_PRESENT
	m0_fom_type_init(&mgmt_run_fom_type, &mgmt_fop_run_fom_type_ops,
			 &m0_mgmt_svc_type, &mgmt_fop_run_sm);
#endif
	return 0;
}

static void mgmt_fop_run_fini(void)
{
}

/** @} end group mgmt_pvt */

/*
 ******************************************************************************
 * Public interfaces
 ******************************************************************************
 */

M0_INTERNAL int m0_mgmt_reqh_service_start(struct m0_reqh_service *service)
{
#ifdef M0_MGMT_SERVICE_PRESENT
	int              rc;
	struct m0_fom   *fom;
	struct mgmt_svc *mgmt_svc;

	M0_PRE(service != NULL);
	M0_PRE(service->rs_reqh != NULL);
	M0_PRE(M0_IN(m0_reqh_state_get(service->rs_reqh),
		     (M0_REQH_ST_MGMT_STARTED, M0_REQH_ST_NORMAL)));

	mgmt_svc = bob_of(service->rs_reqh->rh_mgmt_svc, struct mgmt_svc,
			  ms_reqhs, &mgmt_svc_bob);

	rc = mgmt_run_fom_create(service, &mgmt_run_fom_type,
				 &mgmt_fop_run_fom_ops, &fom);
	if (rc == 0) {
		M0_ASSERT(m0_atomic64_get(&mgmt_svc->ms_run_foms) > 0);
		m0_fom_queue(fom, service->rs_reqh);
	} else {
		m0_reqh_service_failed(service);
	}
	return rc;
#else
	return -ENOSYS;
#endif
}

M0_INTERNAL void m0_mgmt_reqh_services_start_wait(struct m0_reqh *reqh)
{
#ifdef M0_MGMT_SERVICE_PRESENT
	struct m0_clink  clink;
	int64_t          cnt;
	struct mgmt_svc *mgmt_svc;

	M0_PRE(reqh != NULL);
	M0_PRE(M0_IN(m0_reqh_state_get(reqh),
		     (M0_REQH_ST_MGMT_STARTED, M0_REQH_ST_NORMAL,
		      M0_REQH_ST_DRAIN, M0_REQH_ST_SVCS_STOP)));
	M0_PRE(reqh->rh_mgmt_svc != NULL);
	mgmt_svc = bob_of(reqh->rh_mgmt_svc, struct mgmt_svc, ms_reqhs,
			  &mgmt_svc_bob);

	/* Service startup fom termination is signalled on the
	 * rh_sm_grp channel.
	 */
        m0_clink_init(&clink, NULL);
        m0_clink_add_lock(&reqh->rh_sm_grp.s_chan, &clink);

	do {
		m0_sm_group_lock(&reqh->rh_sm_grp);
		while (m0_chan_trywait(&clink))
			; /* drain pending events */
		cnt =  m0_atomic64_get(&mgmt_svc->ms_run_foms);
		M0_ASSERT(cnt >= 0);
		m0_sm_group_unlock(&reqh->rh_sm_grp);
		if (cnt != 0)
			m0_chan_wait(&clink);
	} while (cnt != 0);

	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);
#endif
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
