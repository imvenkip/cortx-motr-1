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
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 09/24/2012
 */

#include "lib/ut.h"
#include "lib/memory.h"
#include "reqh/reqh.h"
#include "cm/cp.h"
#include "cm/cp.c"
#include "sns/repair/cp.h"
#include "sns/repair/cp.c"
#include "cm/ag.h"

static struct m0_reqh      reqh;
static struct m0_semaphore sem;

/* Single thread test vars. */
static struct m0_sns_repair_cp s_sns_cp;
static struct m0_cm_aggr_group s_ag;
static struct m0_bufvec s_bv;

enum {
	THREADS_NR = 17,
};

/* Multithreaded test vars. */
static struct m0_sns_repair_cp m_sns_cp[THREADS_NR];
static struct m0_cm_aggr_group m_ag[THREADS_NR];
static struct m0_bufvec m_bv[THREADS_NR];

static int dummy_cp_read(struct m0_cm_cp *cp)
{
	cp->c_io_op = M0_CM_CP_READ;
	return cp->c_ops->co_phase_next(cp);
}

static int dummy_cp_write(struct m0_cm_cp *cp)
{
	cp->c_io_op = M0_CM_CP_WRITE;
	return cp->c_ops->co_phase_next(cp);
}

static int dummy_cp_io_wait(struct m0_cm_cp *cp)
{
	return cp->c_ops->co_phase_next(cp);
}

static int dummy_cp_xform(struct m0_cm_cp *cp)
{
	return cp->c_ops->co_phase_next(cp);
}

static int dummy_cp_init(struct m0_cm_cp *cp)
{
	int rc = cp_init(cp);
	m0_semaphore_up(&sem);
	return rc;
}

const struct m0_cm_cp_ops m0_sns_repair_cp_dummy_ops = {
        .co_action = {
                [M0_CCP_INIT]  = &dummy_cp_init,
                [M0_CCP_READ]  = &dummy_cp_read,
                [M0_CCP_WRITE] = &dummy_cp_write,
                [M0_CCP_IO_WAIT] = &dummy_cp_io_wait,
                [M0_CCP_XFORM] = &dummy_cp_xform,
                [M0_CCP_SEND]  = &m0_sns_repair_cp_send,
                [M0_CCP_RECV]  = &m0_sns_repair_cp_recv,
                [M0_CCP_FINI]  = &sns_repair_dummy_cp_fini,
        },
        .co_action_nr          = M0_CCP_NR,
        .co_phase_next         = &m0_sns_repair_cp_phase_next,
        .co_invariant          = &cp_invariant,
        .co_home_loc_helper    = &cp_home_loc_helper,
        .co_complete           = &cp_complete,
        .co_free               = &cp_free,
};

/*
 * Dummy fom fini function which finalises the copy packet by skipping the
 * sw_fill functionality.
 */
void dummy_cp_fom_fini(struct m0_fom *fom)
{
	m0_cm_cp_fini(bob_of(fom, struct m0_cm_cp, c_fom, &cp_bob));
}

/*
 * Over-ridden copy packet FOM ops.
 * This is done to bypass the sw_ag_fill call, which is to be tested
 * separately.
 */
static struct m0_fom_ops dummy_cp_fom_ops = {
        .fo_fini          = dummy_cp_fom_fini,
        .fo_tick          = cp_fom_tick,
        .fo_home_locality = cp_fom_locality
};

/*
 * Populates the copy packet and queues it to the request handler
 * for processing.
 */
static void cp_post(struct m0_sns_repair_cp *sns_cp,
		    struct m0_cm_aggr_group *ag, struct m0_bufvec *bv)
{
	struct m0_cm_cp *cp;
	struct m0_stob_id sid = {
			.si_bits = {
				.u_hi = 1,
				.u_lo = 1
			}
		};

	cp = &sns_cp->rc_base;
	m0_cm_cp_init(cp);
        cp->c_ag = ag;
        /* Required to pass the fom invariant. */
        cp->c_fom.fo_fop = (void *)1;
	cp->c_data = bv;
	sns_cp->rc_sid = sid;
	cp->c_ops = &m0_sns_repair_cp_dummy_ops;
	/* Over-ride the fom ops. */
	cp->c_fom.fo_ops = &dummy_cp_fom_ops;
	m0_fom_queue(&cp->c_fom, &reqh);
	m0_semaphore_down(&sem);
}

/*
 * Tests the copy packet fom functionality by posting a single copy packet
 * to the reqh.
 */
static void test_cp_single_thread(void)
{
	m0_semaphore_init(&sem, 0);
	cp_post(&s_sns_cp, &s_ag, &s_bv);
        /*
         * Wait until all the foms in the request handler locality runq are
         * processed.
         */
        m0_reqh_shutdown_wait(&reqh);
	m0_semaphore_fini(&sem);
}

static void cp_op(const int tid)
{
	cp_post(&m_sns_cp[tid], &m_ag[tid], &m_bv[tid]);
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
        m0_reqh_shutdown_wait(&reqh);
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

        rc = m0_reqh_init(&reqh, NULL, (void*)1, (void*)1, (void*)1, (void*)1);
	M0_ASSERT(rc == 0);

	m0_cm_cp_module_init();
        return 0;
}

/* Finalises the request handler. */
static int cm_cp_fini(void)
{
        m0_reqh_fini(&reqh);
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
