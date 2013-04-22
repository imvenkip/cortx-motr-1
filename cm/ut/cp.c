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
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 09/24/2012
 */

#include "ut/ut.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "reqh/reqh.h"
#include "ioservice/io_device.h"
#include "cm/cp.h"
#include "cm/cp.c"
#include "sns/cm/cp.h"
#include "cm/ag.h"
#include "cm/ut/common_service.h"

static struct m0_semaphore     sem;

/* Single thread test vars. */
static struct m0_sns_cm_cp       s_sns_cp;
struct m0_net_buffer             s_nb;
static struct m0_net_buffer_pool nbp;
static struct m0_cm_aggr_group   s_ag;
static struct m0_dbenv           dbenv;

enum {
	THREADS_NR = 17,
};

static int ut_cp_service_start(struct m0_reqh_service *service)
{
	M0_ASSERT(service != NULL);
	return 0;
}

static void ut_cp_service_stop(struct m0_reqh_service *service)
{
	M0_ASSERT(service != NULL);
}

static void ut_cp_service_fini(struct m0_reqh_service *service)
{
	M0_ASSERT(service != NULL);
	m0_free(service);
}

static const struct m0_reqh_service_ops ut_cp_service_ops = {
	.rso_start = ut_cp_service_start,
	.rso_stop = ut_cp_service_stop,
	.rso_fini = ut_cp_service_fini
};

static int ut_cp_service_allocate(struct m0_reqh_service **service,
				  struct m0_reqh_service_type *stype,
				  struct m0_reqh_context *rctx)
{
	struct m0_reqh_service *serv;

	M0_PRE(stype != NULL && service != NULL);

	M0_ALLOC_PTR(serv);
	M0_ASSERT(serv != NULL);

	serv->rs_type = stype;
	serv->rs_ops = &ut_cp_service_ops;
	*service = serv;
	return 0;
}

static const struct m0_reqh_service_type_ops ut_cp_service_type_ops = {
        .rsto_service_allocate = ut_cp_service_allocate
};

M0_REQH_SERVICE_TYPE_DEFINE(ut_cp_service_type,
			    &ut_cp_service_type_ops,
			    "ut-cp",
                            &m0_addb_ct_ut_service);

/* Multithreaded test vars. */
static struct m0_sns_cm_cp m_sns_cp[THREADS_NR];
static struct m0_cm_aggr_group m_ag[THREADS_NR];
static struct m0_net_buffer m_nb[THREADS_NR];

static int dummy_cp_read(struct m0_cm_cp *cp)
{
	cp->c_io_op = M0_CM_CP_READ;
	cp->c_ops->co_phase_next(cp);
	return M0_FSO_AGAIN;
}

static int dummy_cp_write(struct m0_cm_cp *cp)
{
	cp->c_io_op = M0_CM_CP_WRITE;
	cp->c_ops->co_phase_next(cp);
	return M0_FSO_AGAIN;
}

static int dummy_cp_phase(struct m0_cm_cp *cp)
{
	return cp->c_ops->co_phase_next(cp);
}

static int dummy_cp_init(struct m0_cm_cp *cp)
{
	int rc = m0_sns_cm_cp_init(cp);

	m0_semaphore_up(&sem);
	return rc;
}

const struct m0_cm_cp_ops m0_sns_cm_cp_dummy_ops = {
        .co_action = {
                [M0_CCP_INIT]         = &dummy_cp_init,
                [M0_CCP_READ]         = &dummy_cp_read,
                [M0_CCP_WRITE]        = &dummy_cp_write,
                [M0_CCP_IO_WAIT]      = &dummy_cp_phase,
                [M0_CCP_XFORM]        = &dummy_cp_phase,
                [M0_CCP_SW_CHECK]     = &dummy_cp_phase,
                [M0_CCP_SEND]         = &dummy_cp_phase,
		[M0_CCP_SEND_WAIT]    = &dummy_cp_phase,
		[M0_CCP_RECV_INIT]    = &dummy_cp_phase,
		[M0_CCP_RECV_WAIT]    = &dummy_cp_phase,
                [M0_CCP_FINI]         = &m0_sns_cm_cp_fini,
        },
        .co_action_nr          = M0_CCP_NR,
        .co_phase_next         = &m0_sns_cm_cp_phase_next,
        .co_invariant          = &m0_sns_cm_cp_invariant,
        .co_home_loc_helper    = &cp_home_loc_helper,
        .co_complete           = &m0_sns_cm_cp_complete,
        .co_free               = &m0_sns_cm_cp_free,
};

/*
 * Dummy fom fini function which finalises the copy packet by skipping the
 * sw_fill functionality.
 */
void dummy_cp_fom_fini(struct m0_fom *fom)
{
	m0_cm_cp_fini(bob_of(fom, struct m0_cm_cp, c_fom, &cp_bob));
}

void dummy_cp_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	/**
	 * @todo: Do the actual impl, need to set MAGIC, so that
	 * m0_fom_init() can pass
	 */
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;

}

/*
 * Over-ridden copy packet FOM ops.
 * This is done to bypass the sw_ag_fill call, which is to be tested
 * separately.
 */
static struct m0_fom_ops dummy_cp_fom_ops = {
        .fo_fini          = dummy_cp_fom_fini,
        .fo_tick          = cp_fom_tick,
        .fo_home_locality = cp_fom_locality,
	.fo_addb_init     = dummy_cp_fom_addb_init
};

/*
 * Populates the copy packet and queues it to the request handler
 * for processing.
 */
static void cp_post(struct m0_sns_cm_cp *sns_cp, struct m0_cm_aggr_group *ag,
		    struct m0_net_buffer *nb)
{
	struct m0_cm_cp *cp;
	struct m0_stob_id sid = {
			.si_bits = {
				.u_hi = 1,
				.u_lo = 1
			}
		};

	cp = &sns_cp->sc_base;
	cp->c_ag = ag;
	sns_cp->sc_sid = sid;
	cp->c_ops = &m0_sns_cm_cp_dummy_ops;
	m0_cm_cp_init(ag->cag_cm, cp);
	/* Over-ride the fom ops. */
	cp->c_fom.fo_ops = &dummy_cp_fom_ops;
	m0_cm_cp_buf_add(cp, nb);
	m0_fom_queue(&cp->c_fom, &cm_ut_reqh);
	m0_semaphore_down(&sem);
}

/*
 * Tests the copy packet fom functionality by posting a single copy packet
 * to the reqh.
 */
static void test_cp_single_thread(void)
{
	m0_semaphore_init(&sem, 0);
	s_ag.cag_cm = &cm_ut[0].ut_cm;
	s_ag.cag_cp_local_nr = 1;
	s_nb.nb_pool = &nbp;
	cp_post(&s_sns_cp, &s_ag, &s_nb);

        /*
         * Wait until all the foms in the request handler locality runq are
         * processed.
         */
        m0_reqh_fom_domain_idle_wait(&cm_ut_reqh);
	m0_semaphore_fini(&sem);
}

static void cp_op(const int tid)
{
	m_ag[tid].cag_cm = &cm_ut[0].ut_cm;
	m_ag[tid].cag_cp_local_nr = 1;
	m_nb[tid].nb_pool = &nbp;
	cp_post(&m_sns_cp[tid], &m_ag[tid], &m_nb[tid]);
}

/*
 * Tests the copy packet fom functionality by posting multiple copy packets
 * to the reqh.
 */
static void test_cp_multi_thread(void)
{
	int               i;
	struct m0_thread *cp_thread;

	m0_semaphore_init(&sem, 0);

        M0_ALLOC_ARR(cp_thread, THREADS_NR);
        M0_UT_ASSERT(cp_thread != NULL);

	/* Post multiple copy packets to the request handler queue. */
	for (i = 0; i < THREADS_NR; ++i)
		M0_UT_ASSERT(M0_THREAD_INIT(&cp_thread[i], int, NULL, &cp_op, i,
					    "cp_thread_%d", i) == 0);

	for (i = 0; i < THREADS_NR; ++i)
		m0_thread_join(&cp_thread[i]);
        /*
         * Wait until all the foms in the request handler locality runq are
         * processed.
         */
        m0_reqh_fom_domain_idle_wait(&cm_ut_reqh);
        m0_free(cp_thread);
	m0_semaphore_fini(&sem);
}
/*
 * Initialises the request handler since copy packet fom has to be tested using
 * request handler infrastructure.
 */
static int cm_cp_init(void)
{
	int rc;
	rc = m0_dbenv_init(&dbenv, "something", 0);
	M0_ASSERT(rc == 0);

	rc = M0_REQH_INIT(&cm_ut_reqh,
			  .rhia_dtm       = NULL,
			  .rhia_db        = &dbenv,
			  .rhia_mdstore   = (void *)1,
			  .rhia_fol       = (void *)1,
			  .rhia_svc       = (void *)1,
			  .rhia_addb_stob = NULL);
	M0_ASSERT(rc == 0);
	m0_reqh_start(&cm_ut_reqh);
	rc = m0_cm_type_register(&cm_ut_cmt);
	M0_ASSERT(rc == 0);
	cm_ut_service_alloc_init();
	rc = m0_reqh_service_start(cm_ut_service);
	M0_ASSERT(rc == 0);
	rc = m0_ios_poolmach_init(cm_ut_service);
	M0_ASSERT(rc == 0);

        return 0;
}

/* Finalises the request handler. */
static int cm_cp_fini(void)
{
	m0_ios_poolmach_fini(cm_ut_service);
	cm_ut_service_cleanup();
	m0_cm_type_deregister(&cm_ut_cmt);
	m0_reqh_services_terminate(&cm_ut_reqh);
	m0_reqh_fini(&cm_ut_reqh);
	M0_SET0(&cm_ut_reqh);
	m0_dbenv_fini(&dbenv);
	return 0;
}

const struct m0_test_suite cm_cp_ut = {
        .ts_name = "cm-cp-ut",
        .ts_init = &cm_cp_init,
        .ts_fini = &cm_cp_fini,
        .ts_tests = {
                { "cp-single_thread", test_cp_single_thread },
                { "cp-multi_thread", test_cp_multi_thread },
                { NULL, NULL }
        }
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
