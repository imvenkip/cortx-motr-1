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

static struct c2_reqh      reqh;
static struct c2_semaphore sem;

/** Single thread test vars. */
static struct c2_sns_repair_cp s_sns_cp;
static struct c2_cm_aggr_group s_ag;
static struct c2_bufvec s_bv;

enum {
	THREADS_NR = 17,
};

/** Multithreaded test vars. */
static struct c2_sns_repair_cp m_sns_cp[THREADS_NR];
static struct c2_cm_aggr_group m_ag[THREADS_NR];
static struct c2_bufvec m_bv[THREADS_NR];

static int dummy_cp_xform(struct c2_cm_cp *cp)
{
	cp->c_ops->co_phase_next(cp);
	return C2_FSO_AGAIN;
}

static int dummy_cp_init(struct c2_cm_cp *cp)
{
	int rc = cp_init(cp);
	c2_semaphore_up(&sem);
	return rc;
}

const struct c2_cm_cp_ops c2_sns_repair_cp_dummy_ops = {
        .co_action = {
                [C2_CCP_INIT]  = &dummy_cp_init,
                [C2_CCP_READ]  = &cp_read,
                [C2_CCP_WRITE] = &cp_write,
                [C2_CCP_XFORM] = &dummy_cp_xform,
                [C2_CCP_SEND]  = &cp_send,
                [C2_CCP_RECV]  = &cp_recv,
                [C2_CCP_FINI]  = &cp_fini,
                [SRP_IO_WAIT]  = &cp_io_wait
        },
        .co_action_nr          = C2_CCP_NR,
        .co_phase_next         = &cp_phase_next,
        .co_invariant          = &cp_invariant,
        .co_home_loc_helper    = &cp_home_loc_helper,
        .co_complete           = &cp_complete,
        .co_free               = &cp_free,
};

/**
 * Dummy fom fini function which finalises the copy packet by skipping the
 * sw_fill functionality.
 */
void dummy_cp_fom_fini(struct c2_fom *fom)
{
	c2_cm_cp_fini(bob_of(fom, struct c2_cm_cp, c_fom, &cp_bob));
}

/**
 * Over-ridden copy packet FOM ops.
 * This is done to bypass the sw_ag_fill call, which is to be tested
 * separately.
 */
static struct c2_fom_ops dummy_cp_fom_ops = {
        .fo_fini          = dummy_cp_fom_fini,
        .fo_tick          = cp_fom_tick,
        .fo_home_locality = cp_fom_locality
};

/**
 * Populates the copy packet and queues it to the request handler
 * for processing.
 */
static void cp_post(struct c2_sns_repair_cp *sns_cp,
		    struct c2_cm_aggr_group *ag, struct c2_bufvec *bv)
{
	struct c2_cm_cp *cp;
	struct c2_stob_id sid = {
			.si_bits = {
				.u_hi = 1,
				.u_lo = 1
			}
		};

	cp = &sns_cp->rc_base;
	c2_cm_cp_init(cp);
        cp->c_ag = ag;
        /** Required to pass the fom invariant. */
        cp->c_fom.fo_fop = (void *)1;
	cp->c_data = bv;
	sns_cp->rc_sid = sid;
	cp->c_ops = &c2_sns_repair_cp_dummy_ops;
	/** Over-ride the fom ops. */
	cp->c_fom.fo_ops = &dummy_cp_fom_ops;
	c2_fom_queue(&cp->c_fom, &reqh);
	c2_semaphore_down(&sem);
}

/**
 * Tests the copy packet fom functionality by posting a single copy packet
 * to the reqh.
 */
static void test_cp_single_thread(void)
{
	c2_semaphore_init(&sem, 0);
	cp_post(&s_sns_cp, &s_ag, &s_bv);
        /**
         * Wait until all the foms in the request handler locality runq are
         * processed.
         */
        c2_reqh_shutdown_wait(&reqh);
	c2_semaphore_fini(&sem);
}

static void cp_op(const int tid)
{
	cp_post(&m_sns_cp[tid], &m_ag[tid], &m_bv[tid]);
}

/**
 * Tests the copy packet fom functionality by posting multiple copy packets
 * to the reqh.
 */
static void test_cp_multi_thread(void)
{
	int               i;
	struct c2_thread *cp_thread;

	c2_semaphore_init(&sem, 0);

        C2_ALLOC_ARR(cp_thread, THREADS_NR);
        C2_UT_ASSERT(cp_thread != NULL);

	/** Post multiple copy packets to the request handler queue. */
	for (i = 0; i < THREADS_NR; ++i)
		C2_UT_ASSERT(C2_THREAD_INIT(&cp_thread[i], int, NULL, &cp_op, i,
					    "cp_thread_%d", i) == 0);

	for (i = 0; i < THREADS_NR; ++i)
		c2_thread_join(&cp_thread[i]);

        /**
         * Wait until all the foms in the request handler locality runq are
         * processed.
         */
        c2_reqh_shutdown_wait(&reqh);
        c2_free(cp_thread);
	c2_semaphore_fini(&sem);
}

/**
 * Initialise the request handler since copy packet fom has to be tested using
 * request handler infrastructure.
 */
static int cm_cp_init(void)
{
        c2_reqh_init(&reqh, NULL, (void*)1, (void*)1, (void*)1, (void*)1);
	c2_cm_cp_module_init();
        return 0;
}

/**
 * Finalises the request handler.
 */
static int cm_cp_fini(void)
{
        c2_reqh_fini(&reqh);
        return 0;
}

const struct c2_test_suite cm_cp_ut = {
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
