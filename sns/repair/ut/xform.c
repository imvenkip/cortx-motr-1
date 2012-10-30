/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 08/16/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/misc.h"
#include "lib/processor.h"
#include "lib/ut.h"
#include "reqh/reqh.h"
#include "sns/repair/cp.h"
#include "sns/repair/ag.h"
#include "sns/repair/xform.c"

enum {
	NR = 16,
	SEG_SIZE = 256,
	CP_SINGLE = 1,
	CP_MULTI = 512,
};

static struct c2_reqh      reqh;
static struct c2_semaphore sem;

/* Global structures for single copy packet test. */
static struct c2_sns_repair_ag s_sag;
static struct c2_cm_cp         s_cp;
static struct c2_bufvec        s_bv;

/* Global structures for multiple copy packet test. */
static struct c2_sns_repair_ag m_sag;
static struct c2_cm_cp         m_cp[CP_MULTI];
static struct c2_bufvec        m_bv[CP_MULTI];

/* Global structures for testing bufvec xor correctness. */
struct c2_bufvec src;
struct c2_bufvec dst;
struct c2_bufvec xor;

static uint64_t cp_single_get(struct c2_cm_aggr_group *ag)
{
	return CP_SINGLE;
}

static const struct c2_cm_aggr_group_ops group_single_ops = {
        .cago_local_cp_nr = &cp_single_get,
};

static uint64_t cp_multi_get(struct c2_cm_aggr_group *ag)
{
	return CP_MULTI;
}

static const struct c2_cm_aggr_group_ops group_multi_ops = {
        .cago_local_cp_nr = &cp_multi_get,
};

/* Populates the bufvec with a character value. */
static void bv_populate(struct c2_bufvec *b, char data)
{
	int i;

	C2_UT_ASSERT(b != NULL);
        C2_UT_ASSERT(c2_bufvec_alloc(b, NR, SEG_SIZE) == 0);
        C2_UT_ASSERT(b->ov_vec.v_nr == NR);
        for (i = 0; i < NR; ++i) {
		C2_UT_ASSERT(b->ov_vec.v_count[i] == SEG_SIZE);
		C2_UT_ASSERT(b->ov_buf[i] != NULL);
		memset(b->ov_buf[i], data, SEG_SIZE);
        }
}

/* Compares 2 bufvecs and asserts if not equal. */
static void bv_compare(struct c2_bufvec *b1, struct c2_bufvec *b2)
{
	int i;

	C2_UT_ASSERT(b1 != NULL);
	C2_UT_ASSERT(b2 != NULL);
        C2_UT_ASSERT(b1->ov_vec.v_nr == NR);
        C2_UT_ASSERT(b2->ov_vec.v_nr == NR);

        for (i = 0; i < NR; ++i) {
		C2_UT_ASSERT(b1->ov_vec.v_count[i] == SEG_SIZE);
		C2_UT_ASSERT(b1->ov_buf[i] != NULL);
		C2_UT_ASSERT(b2->ov_vec.v_count[i] == SEG_SIZE);
		C2_UT_ASSERT(b2->ov_buf[i] != NULL);
		C2_UT_ASSERT(memcmp(b1->ov_buf[i], b2->ov_buf[i],
				    SEG_SIZE) == 0);
        }
}

static inline void bv_free(struct c2_bufvec *b)
{
        c2_bufvec_free(b);
}

static size_t dummy_fom_locality(const struct c2_fom *fom)
{
	/* By default, use locality0. */
	return 0;
}

/* Dummy fom state routine to emulate only selective copy packet states. */
static int dummy_fom_tick(struct c2_fom *fom)
{
	struct c2_cm_cp *cp = container_of(fom, struct c2_cm_cp, c_fom);

	switch (c2_fom_phase(fom)) {
	case C2_FOM_PHASE_INIT:
		c2_fom_phase_set(fom, C2_CCP_XFORM);
		c2_semaphore_up(&sem);
		return cp->c_ops->co_action[C2_CCP_XFORM](cp);
	case C2_CCP_FINI:
                return C2_FSO_WAIT;
	case C2_CCP_WRITE:
		c2_fom_phase_set(fom, C2_CCP_FINI);
                return C2_FSO_WAIT;
	default:
		C2_IMPOSSIBLE("Bad State");
	}
}

static void single_cp_fom_fini(struct c2_fom *fom)
{
	struct c2_cm_cp *cp = container_of(fom, struct c2_cm_cp, c_fom);

	bv_free(cp->c_data);
	c2_cm_cp_fini(cp);
}

static void multiple_cp_fom_fini(struct c2_fom *fom)
{
	struct c2_cm_cp *cp = container_of(fom, struct c2_cm_cp, c_fom);

	bv_free(cp->c_data);
	c2_cm_cp_fini(cp);
}

/* Over-ridden copy packet FOM ops. */
static struct c2_fom_ops single_cp_fom_ops = {
        .fo_fini          = single_cp_fom_fini,
        .fo_tick          = dummy_fom_tick,
        .fo_home_locality = dummy_fom_locality
};

/* Over-ridden copy packet FOM ops. */
static struct c2_fom_ops multiple_cp_fom_ops = {
        .fo_fini          = multiple_cp_fom_fini,
        .fo_tick          = dummy_fom_tick,
        .fo_home_locality = dummy_fom_locality
};

static void cp_prepare(struct c2_cm_cp *cp, struct c2_bufvec *bv,
		       struct c2_sns_repair_ag *sns_ag,
		       char data, struct c2_fom_ops *cp_fom_ops)
{
	C2_UT_ASSERT(cp != NULL);
	C2_UT_ASSERT(bv != NULL);
	C2_UT_ASSERT(sns_ag != NULL);

	bv_populate(bv, data);
	cp->c_ag = &sns_ag->sag_base;
	c2_cm_cp_init(cp);
	cp->c_data = bv;
	cp->c_fom.fo_ops = cp_fom_ops;
	cp->c_ops = &c2_sns_repair_cp_ops;
	/* Required to pass the fom invariant */
	cp->c_fom.fo_fop = (void *)1;
}

/*
 * Test to check that single copy packet is treated as passthrough by the
 * transformation function.
 */
static void test_single_cp(void)
{
	c2_semaphore_init(&sem, 0);
	c2_atomic64_set(&s_sag.sag_base.cag_transformed_cp_nr, 0);
	cp_prepare(&s_cp, &s_bv, &s_sag, 'e', &single_cp_fom_ops);
	s_cp.c_ag->cag_ops = &group_single_ops;
	c2_fom_queue(&s_cp.c_fom, &reqh);

	/* Wait till ast gets posted. */
	c2_semaphore_down(&sem);
	/*
	 * Wait until all the foms in the request handler locality runq are
	 * processed. This is required for further validity checks.
	 */
	c2_reqh_shutdown_wait(&reqh);

	/*
	 * These asserts ensure that the single copy packet has been treated
	 * as passthrough.
	 */
        C2_UT_ASSERT(c2_atomic64_get(&s_sag.sag_base.cag_transformed_cp_nr) ==
		     0);
        C2_UT_ASSERT(s_sag.sag_base.cag_cp_nr == 1);
	c2_semaphore_fini(&sem);
}

/*
 * Test to check that multiple copy packets are collected by the
 * transformation function.
 */
static void test_multiple_cp(void)
{
	int i;

	c2_semaphore_init(&sem, 0);
	c2_atomic64_set(&m_sag.sag_base.cag_transformed_cp_nr, 0);
	for (i = 0; i < CP_MULTI; ++i) {
		cp_prepare(&m_cp[i], &m_bv[i], &m_sag, 'r',
			   &multiple_cp_fom_ops);
		m_cp[i].c_ag->cag_ops = &group_multi_ops;
		c2_fom_queue(&m_cp[i].c_fom, &reqh);
		c2_semaphore_down(&sem);
	}

	/*
	 * Wait until the fom in the request handler locality runq is
	 * processed. This is required for further validity checks.
	 */
	c2_reqh_shutdown_wait(&reqh);

	/*
	 * These asserts ensure that all the copy packets have been collected
	 * by the transformation function.
	 */
        C2_UT_ASSERT(c2_atomic64_get(&m_sag.sag_base.cag_transformed_cp_nr) ==
		     CP_MULTI);
	C2_UT_ASSERT(m_sag.sag_base.cag_cp_nr == CP_MULTI);
	c2_semaphore_fini(&sem);
}

/*
 * Initialises the request handler since copy packet fom has to be tested using
 * request handler infrastructure.
 */
static int xform_init(void)
{
	c2_reqh_init(&reqh, NULL, (void*)1, (void*)1, (void*)1, (void*)1);
	return 0;
}

static int xform_fini(void)
{
	c2_reqh_fini(&reqh);

	return 0;
}

/* Tests the correctness of the bufvec_xor function. */
static void test_bufvec_xor()
{
	bv_populate(&src, '4');
	bv_populate(&dst, 'D');
	/*
	 * Actual result is anticipated and stored in new bufvec, which is
	 * used for comparison with xor'ed output.
	 * 4 XOR D = p
	 */
	bv_populate(&xor, 'p');
	bufvec_xor(&dst, &src, SEG_SIZE * NR);
	bv_compare(&dst, &xor);
	bv_free(&src);
	bv_free(&dst);
	bv_free(&xor);
}

const struct c2_test_suite snsrepair_xform_ut = {
        .ts_name = "snsrepair_xform-ut",
        .ts_init = &xform_init,
        .ts_fini = &xform_fini,
        .ts_tests = {
                { "single_cp_passthrough", test_single_cp },
                { "multiple_cp_bufvec_xor", test_multiple_cp },
                { "bufvec_xor_correctness", test_bufvec_xor },
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
