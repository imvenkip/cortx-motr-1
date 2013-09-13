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
   @page ADDB-DLD-SVC-pstats Periodic Posting of Statistics
   Periodic posting of statistics is done by means of a dedicated @ref fom "FOM"
   represented by the ::addb_post_fom structure.
   The FOM is created by the addb_pfom_start() subroutine invoked by the
   addb_service_start() service operation.

   The FOM transitions through the following phases:
   @dot
   digraph pstats {
       S0 [label="Init"]
       S1 [label="ComputeTimeout"]
       S2 [label="Sleep"]
       S3 [label="Post" ]
       S4 [label="Fini" ]
       S0 -> S1 [label="*"];
       S1 -> S2 [label="*"];
       S2 -> S2 [label="NotReady"];
       S2 -> S3 [label="Ready"];
       S2 -> S4 [label="Stopped"];
       S3 -> S1 [label="*"];
   }
   @enddot
   - The FOM starts in the Init phase then transitions to the ComputeTimeout
   phase.  It forces the next phase to compute the next posting epoch based
   on the current time.
   - In the ComputeTimeout phase the FOM calculates the absolute time of the
   next posting epoch.  The next epoch is computed relative to the previous
   epoch so as to reduce the impact of the actual time it takes to post the
   statistics.  However, if the current time has already advanced significantly
   toward the next epoch, then it will compute the next epoch against current
   time.  The latter computation is forced on the first iteration, but could
   conceivably happen at run time because a ready FOM is subject to scheduling
   delays.  The FOM transitions to the Sleep phase next.
   - In the Sleep phase, it checks to see if the FOM has been shutdown, and
   if so, will transition to the Fini phase.  If not shutdown, it checks to
   see if the current time exceeds the next posting epoch, and if so will
   transition to the Post phase.  Otherwise it will block the FOM until
   the next posting epoch.
   - The Post phase will invoke m0_reqh_stats_post_addb() to post pending
   statistics.  It will then transition to the ComputeTimeout phase to repeat
   the cycle.

   The period of the posting fom is set on creation from the value of
   the ::addb_pfom_period global variable.

   The posting fom is launched only if the ::addb_svc_start_pfom global
   permits.  This control is provided for unit testing.

   The FOM is terminated by invoking addb_pfom_stop().  The subroutine
   posts an AST to cancel its timer and force it to stop itself.
 */

/* This file is designed to be included by addb/addb.c */

/**
   @ingroup addb_svc_pvt
   @{
 */

static const struct m0_bob_type addb_pfom_bob = {
	.bt_name = "addb pfom",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct addb_post_fom, pf_magic),
	.bt_magix = M0_ADDB_PFOM_MAGIC,
	.bt_check = NULL
};

M0_BOB_DEFINE(static, &addb_pfom_bob, addb_post_fom);

/*
 ******************************************************************************
 * ADDB Statistics Posting FOM Type
 ******************************************************************************
 */
enum addb_pfom_phase {
	ADDB_PFOM_PHASE_INIT  = M0_FOM_PHASE_INIT,
	ADDB_PFOM_PHASE_FINI  = M0_FOM_PHASE_FINISH,
	ADDB_PFOM_PHASE_CTO   = M0_FOM_PHASE_NR,
	ADDB_PFOM_PHASE_SLEEP,
	ADDB_PFOM_PHASE_POST,
};

static struct m0_sm_state_descr addb_pfom_state_descr[] = {
        [ADDB_PFOM_PHASE_INIT] = {
                .sd_flags       = M0_SDF_INITIAL,
                .sd_name        = "Init",
                .sd_allowed     = M0_BITS(ADDB_PFOM_PHASE_CTO)
        },
        [ADDB_PFOM_PHASE_CTO] = {
                .sd_flags       = 0,
                .sd_name        = "ComputeTimeOut",
                .sd_allowed     = M0_BITS(ADDB_PFOM_PHASE_SLEEP)
        },
        [ADDB_PFOM_PHASE_SLEEP] = {
                .sd_flags       = 0,
                .sd_name        = "Sleep",
                .sd_allowed     = M0_BITS(ADDB_PFOM_PHASE_POST,
					  ADDB_PFOM_PHASE_SLEEP,
					  ADDB_PFOM_PHASE_FINI)
        },
        [ADDB_PFOM_PHASE_POST] = {
                .sd_flags       = 0,
                .sd_name        = "Post",
                .sd_allowed     = M0_BITS(ADDB_PFOM_PHASE_CTO)
        },
        [ADDB_PFOM_PHASE_FINI] = {
                .sd_flags       = M0_SDF_TERMINAL,
                .sd_name        = "Fini",
                .sd_allowed     = 0
        },
};

static struct m0_sm_conf addb_pfom_sm_conf = {
	.scf_name = "addb-pfom-sm",
	.scf_nr_states = ARRAY_SIZE(addb_pfom_state_descr),
	.scf_state = addb_pfom_state_descr
};

static const struct m0_fom_type_ops addb_pfom_type_ops = {
        .fto_create = NULL
};

static struct m0_fom_type addb_pfom_type;

/*
 ******************************************************************************
 * ADDB Statistics Posting FOM
 ******************************************************************************
 */

/** UT hook to track a singleton FOM */
static bool the_addb_pfom_started;

enum {
	/**
	   Tolerance on the posting epoch when re-computing the next epoch.
	   The number is expressed as a fraction of the posting period.  The
	   greater the number the narrower the tolerance.
	 */
	M0_ADDB_PFOM_PERIOD_FRAC_TOLERANCE = 10,
};

static bool addb_pfom_invariant(const struct addb_post_fom *pfom)
{
	return addb_post_fom_bob_check(pfom);
}

static void addb_pfom_fo_fini(struct m0_fom *fom)
{
        struct addb_post_fom   *pfom = bob_of(fom, struct addb_post_fom, pf_fom,
					      &addb_pfom_bob);
	struct addb_svc        *svc = container_of(pfom, struct addb_svc,
						   as_pfom);
	struct m0_reqh_service *rsvc = &svc->as_reqhs;

	M0_ENTRY();

	m0_fom_fini(fom);
	m0_fom_timeout_fini(&pfom->pf_timeout);
	addb_post_fom_bob_fini(pfom);

	/*
	 * Mustn't free as the fom is embedded in the service object, but
	 * notify UT waiters.
	 */
	m0_mutex_lock(&rsvc->rs_mutex);
	pfom->pf_running = false;
	m0_cond_broadcast(&svc->as_cond);
	the_addb_pfom_started = false;
	M0_LOG(M0_DEBUG, "done");
	m0_mutex_unlock(&rsvc->rs_mutex);
}

static size_t addb_pfom_fo_locality(const struct m0_fom *fom)
{
	return 1; // well, why not?
}

static int addb_pfom_fo_tick(struct m0_fom *fom)
{
        struct addb_post_fom   *pfom = bob_of(fom, struct addb_post_fom, pf_fom,
					      &addb_pfom_bob);
	struct addb_svc        *svc = container_of(pfom, struct addb_svc,
						   as_pfom);
	struct m0_reqh         *reqh = svc->as_reqhs.rs_reqh;
	struct m0_reqh_service *rsvc = &svc->as_reqhs;
	int                     rc = M0_FSO_AGAIN;
	m0_time_t               now;
	int                     err = 0;
	M0_ENTRY();

	switch (m0_fom_phase(fom)) {
	case ADDB_PFOM_PHASE_INIT:
		M0_LOG(M0_DEBUG, "init");
		m0_mutex_lock(&rsvc->rs_mutex);
		the_addb_pfom_started = true;
		m0_cond_broadcast(&svc->as_cond); /* for UT */
		m0_mutex_unlock(&rsvc->rs_mutex);
		m0_fom_phase_set(fom, ADDB_PFOM_PHASE_CTO);
		break;
	case ADDB_PFOM_PHASE_CTO:
		M0_LOG(M0_DEBUG, "cto");
		now = m0_time_now();
		if (now < pfom->pf_next_post + pfom->pf_tolerance)
			pfom->pf_next_post += pfom->pf_period;
		else
			pfom->pf_next_post = now + pfom->pf_period;
		m0_fom_phase_set(fom, ADDB_PFOM_PHASE_SLEEP);
		break;
	case ADDB_PFOM_PHASE_SLEEP:
		if (pfom->pf_shutdown) {
			M0_LOG(M0_DEBUG, "fini");
			m0_fom_phase_set(fom, ADDB_PFOM_PHASE_FINI);
			rc = M0_FSO_WAIT;
			break;
		}
		now = m0_time_now();
		if (now >= pfom->pf_next_post) {
			m0_fom_phase_set(fom, ADDB_PFOM_PHASE_POST);
			break;
		}
		m0_fom_timeout_fini(&pfom->pf_timeout);
		m0_fom_timeout_init(&pfom->pf_timeout);
		m0_fom_timeout_wait_on(&pfom->pf_timeout, &pfom->pf_fom,
				       pfom->pf_next_post);
		M0_LOG(M0_DEBUG, "wait");
		rc = M0_FSO_WAIT;
		break;
	case ADDB_PFOM_PHASE_POST:
		M0_LOG(M0_DEBUG, "post");
		m0_reqh_stats_post_addb(reqh);

		if (reqh->rh_addb_monitoring_ctx.amc_stats_conn != NULL)
			err = m0_addb_monitor_summaries_post(reqh, pfom);
		/**
		 * In case of summaries posting failure, just log error.
		 * We do not terminate the fom.
		 */
		if (err != 0)
			M0_LOG(M0_ERROR, "addb summary posting failed");

/** Only needed for stobsink, so should not be called in kernel */
#ifndef __KERNEL__
		if (reqh->rh_addb_mc.am_sink->rs_skulk != NULL)
			(*reqh->rh_addb_mc.am_sink->rs_skulk)
				(&reqh->rh_addb_mc);
#endif
		m0_fom_phase_set(fom, ADDB_PFOM_PHASE_CTO);
		break;
	default:
		M0_IMPOSSIBLE("Phasors were not on stun!");
	}

	M0_RETURN(rc);
}

static void addb_pfom_fo_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
        struct addb_post_fom *pfom = bob_of(fom, struct addb_post_fom, pf_fom,
					    &addb_pfom_bob);
	struct addb_svc      *svc = container_of(pfom, struct addb_svc,
						 as_pfom);

	M0_ADDB_CTX_INIT(mc, &fom->fo_addb_ctx, &m0_addb_ct_addb_pfom,
			 &svc->as_reqhs.rs_addb_ctx);
}

static const struct m0_fom_ops addb_pfom_ops = {
        .fo_fini          = addb_pfom_fo_fini,
        .fo_tick          = addb_pfom_fo_tick,
        .fo_home_locality = addb_pfom_fo_locality,
	.fo_addb_init     = addb_pfom_fo_addb_init
};

/*
 ******************************************************************************
 * Interfaces
 ******************************************************************************
 */

/**
   The periodicity set at creation.
   Mainly for UT usage but could eventually be set via config parameters.
 */
static m0_time_t addb_pfom_period = M0_MKTIME(M0_ADDB_DEF_STAT_PERIOD_S, 0);

/**
   Starts the statistics posting FOM.
   @param svc The ADDB service structure. The embedded as_pfom field is the FOM.
 */
static void addb_pfom_start(struct addb_svc *svc)
{
	struct addb_post_fom *pfom = &svc->as_pfom;
	struct m0_fom        *fom = &pfom->pf_fom;
	struct m0_reqh       *reqh = svc->as_reqhs.rs_reqh;

	M0_ENTRY();
	M0_PRE(addb_svc_invariant(svc));

	m0_rwlock_read_lock(&reqh->rh_rwlock);

	addb_post_fom_bob_init(pfom);
	M0_POST(addb_pfom_invariant(pfom));

	m0_fom_init(fom, &addb_pfom_type, &addb_pfom_ops, NULL, NULL,
		    reqh, svc->as_reqhs.rs_type);
	m0_fom_timeout_init(&pfom->pf_timeout);
	pfom->pf_period = addb_pfom_period;
	pfom->pf_tolerance = pfom->pf_period
		/ M0_ADDB_PFOM_PERIOD_FRAC_TOLERANCE;
	pfom->pf_next_post = 0; /* force the first timeout calculation */
	pfom->pf_running = true;

        M0_PRE(m0_fom_phase(fom) == ADDB_PFOM_PHASE_INIT);
	m0_fom_queue(fom, reqh);

	m0_rwlock_read_unlock(&reqh->rh_rwlock);
}

/**
   AST callback to safely stop the FOM.
 */
static void addb_pfom_stop_cb(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
        struct addb_post_fom *pfom = bob_of(ast, struct addb_post_fom,
					    pf_ast, &addb_pfom_bob);

	M0_LOG(M0_DEBUG, "pfom_stop_cb: %d\n", (int)pfom->pf_running);
	if (pfom->pf_running) {
		if (pfom->pf_timeout.to_cb.fc_fom != NULL)
			m0_fom_timeout_cancel(&pfom->pf_timeout);
		if (m0_fom_is_waiting(&pfom->pf_fom))
			m0_fom_ready(&pfom->pf_fom);
		pfom->pf_shutdown = true;
	}
}

/**
   Initiates the termination of the statistics posting FOM.
   Uses the service mutex internally.
   Blocks until the FOM terminates.
 */
static void addb_pfom_stop(struct addb_svc *svc)
{
        struct addb_post_fom   *pfom = &svc->as_pfom;
	struct m0_fom          *fom  = &pfom->pf_fom;
	struct m0_reqh_service *rsvc = &svc->as_reqhs;

	M0_ENTRY();

	M0_PRE(addb_svc_invariant(svc));
	M0_PRE(m0_mutex_is_not_locked(&rsvc->rs_mutex));

	m0_mutex_lock(&rsvc->rs_mutex);
	if (pfom->pf_running) {
		M0_ASSERT(addb_pfom_invariant(pfom));

		M0_LOG(M0_DEBUG, "posting pfom stop ast");
		pfom->pf_ast.sa_cb = addb_pfom_stop_cb;
		m0_sm_ast_post(&fom->fo_loc->fl_group, &pfom->pf_ast);

		M0_LOG(M0_DEBUG, "waiting for pfom to stop");
		while (pfom->pf_running)
			m0_cond_wait(&svc->as_cond);
	}
	m0_mutex_unlock(&rsvc->rs_mutex);
}

/**
   Initializes the statistics posting FOM module.
 */
M0_INTERNAL int addb_pfom_mod_init(void)
{
	m0_fom_type_init(&addb_pfom_type, &addb_pfom_type_ops, NULL,
			 &addb_pfom_sm_conf);
	return 0;
}

/**
   Finalizes the statistics posting FOM module.
 */
static void addb_pfom_mod_fini(void)
{
}

/** @} end group addb_pvt */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
