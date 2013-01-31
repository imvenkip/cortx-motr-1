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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 08/16/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "reqh/reqh.h"
#include "mero/setup.h"
#include "sns/cm/xform.c"
#include "sns/cm/ut/cp_common.h"

enum {
	CP_SINGLE = 1,
	CP_MULTI = 512,
	SEG_NR = 16,
	SEG_SIZE = 4096,
};

static struct m0_reqh      *reqh;
static struct m0_semaphore  sem;

/* Global structures for single copy packet test. */
static struct m0_sns_cm_ag s_sag;
static struct m0_cm_cp     s_cp;
static struct m0_bufvec    s_bv;

/* Global structures for multiple copy packet test. */
static struct m0_sns_cm_ag m_sag;
static struct m0_cm_cp     m_cp[CP_MULTI];
static struct m0_bufvec    m_bv[CP_MULTI];

/* Global structures for testing bufvec xor correctness. */
struct m0_bufvec src;
struct m0_bufvec dst;
struct m0_bufvec xor;

static uint64_t cp_single_get(const struct m0_cm_aggr_group *ag)
{
	return CP_SINGLE;
}

static const struct m0_cm_aggr_group_ops group_single_ops = {
	.cago_local_cp_nr = &cp_single_get,
};

static uint64_t cp_multi_get(const struct m0_cm_aggr_group *ag)
{
	return CP_MULTI;
}

static const struct m0_cm_aggr_group_ops group_multi_ops = {
	.cago_local_cp_nr = &cp_multi_get,
};

static size_t dummy_fom_locality(const struct m0_fom *fom)
{
	/* By default, use locality0. */
	return 0;
}

/* Dummy fom state routine to emulate only selective copy packet states. */
static int dummy_fom_tick(struct m0_fom *fom)
{
	struct m0_cm_cp *cp = container_of(fom, struct m0_cm_cp, c_fom);

	switch (m0_fom_phase(fom)) {
	case M0_FOM_PHASE_INIT:
		m0_fom_phase_set(fom, M0_CCP_XFORM);
		m0_semaphore_up(&sem);
		return cp->c_ops->co_action[M0_CCP_XFORM](cp);
	case M0_CCP_FINI:
		return M0_FSO_WAIT;
	case M0_CCP_WRITE:
		m0_fom_phase_set(fom, M0_CCP_IO_WAIT);
		return M0_FSO_AGAIN;
	case M0_CCP_IO_WAIT:
		m0_fom_phase_set(fom, M0_CCP_FINI);
		return M0_FSO_WAIT;
	default:
		M0_IMPOSSIBLE("Bad State");
		return 0;
	}
}

static void dummy_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
        /**
         * @todo: Do the actual impl, need to set MAGIC, so that
         * m0_fom_init() can pass
         */
        fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

static void single_cp_fom_fini(struct m0_fom *fom)
{
	struct m0_cm_cp *cp = container_of(fom, struct m0_cm_cp, c_fom);

	bv_free(cp->c_data);
	m0_cm_cp_fini(cp);
}

static void multiple_cp_fom_fini(struct m0_fom *fom)
{
	struct m0_cm_cp *cp = container_of(fom, struct m0_cm_cp, c_fom);

	bv_free(cp->c_data);
	m0_cm_cp_fini(cp);
}

/* Over-ridden copy packet FOM ops. */
static struct m0_fom_ops single_cp_fom_ops = {
	.fo_fini          = single_cp_fom_fini,
	.fo_tick          = dummy_fom_tick,
	.fo_home_locality = dummy_fom_locality,
	.fo_addb_init     = dummy_fom_addb_init
};

/* Over-ridden copy packet FOM ops. */
static struct m0_fom_ops multiple_cp_fom_ops = {
	.fo_fini          = multiple_cp_fom_fini,
	.fo_tick          = dummy_fom_tick,
	.fo_home_locality = dummy_fom_locality,
	.fo_addb_init     = dummy_fom_addb_init
};

/*
 * Test to check that single copy packet is treated as passthrough by the
 * transformation function.
 */
static void test_single_cp(void)
{
	m0_semaphore_init(&sem, 0);
	s_sag.sag_base.cag_transformed_cp_nr = 0;
	cp_prepare(&s_cp, &s_bv, SEG_NR, SEG_SIZE, &s_sag, 'e',
		   &single_cp_fom_ops, reqh);
	s_cp.c_ag->cag_ops = &group_single_ops;
	s_cp.c_ag->cag_cp_nr = s_cp.c_ag->cag_ops->cago_local_cp_nr(s_cp.c_ag);
	m0_fom_queue(&s_cp.c_fom, reqh);

	/* Wait till ast gets posted. */
	m0_semaphore_down(&sem);
	/*
	 * Wait until all the foms in the request handler locality runq are
	 * processed. This is required for further validity checks.
	 */
	m0_reqh_fom_domain_idle_wait(reqh);

	/*
	 * These asserts ensure that the single copy packet has been treated
	 * as passthrough.
	 */
	M0_UT_ASSERT(s_sag.sag_base.cag_transformed_cp_nr == 0);
	M0_UT_ASSERT(s_sag.sag_base.cag_cp_nr == 1);
	m0_semaphore_fini(&sem);
}

/*
 * Test to check that multiple copy packets are collected by the
 * transformation function.
 */
static void test_multiple_cp(void)
{
	int i;

	m0_semaphore_init(&sem, 0);
	m_sag.sag_base.cag_transformed_cp_nr = 0;
	for (i = 0; i < CP_MULTI; ++i) {
		cp_prepare(&m_cp[i], &m_bv[i], SEG_NR, SEG_SIZE, &m_sag, 'r',
			   &multiple_cp_fom_ops, reqh);
		m_cp[i].c_ag->cag_ops = &group_multi_ops;
		m_cp[i].c_ag->cag_cp_nr =
			m_cp[i].c_ag->cag_ops->cago_local_cp_nr(m_cp[i].c_ag);
		m0_fom_queue(&m_cp[i].c_fom, reqh);
		m0_semaphore_down(&sem);
	}

	/*
	 * Wait until the fom in the request handler locality runq is
	 * processed. This is required for further validity checks.
	 */
	m0_reqh_fom_domain_idle_wait(reqh);

	/*
	 * These asserts ensure that all the copy packets have been collected
	 * by the transformation function.
	 */
	M0_UT_ASSERT(m_sag.sag_base.cag_transformed_cp_nr == CP_MULTI);
	M0_UT_ASSERT(m_sag.sag_base.cag_cp_nr == CP_MULTI);
	m0_semaphore_fini(&sem);
}

/*
 * Initialises the request handler since copy packet fom has to be tested using
 * request handler infrastructure.
 */
static int xform_init(void)
{
	int rc;

	rc = cs_init(&sctx);
	M0_ASSERT(rc == 0);

	reqh = m0_cs_reqh_get(&sctx, "sns_cm");
	M0_ASSERT(reqh != NULL);
	return 0;
}

static int xform_fini(void)
{
        cs_fini(&sctx);
        return 0;
}

/* Tests the correctness of the bufvec_xor function. */
static void test_bufvec_xor()
{
	bv_populate(&src, '4', SEG_NR, SEG_SIZE);
	bv_populate(&dst, 'D', SEG_NR, SEG_SIZE);
	/*
	 * Actual result is anticipated and stored in new bufvec, which is
	 * used for comparison with xor'ed output.
	 * 4 XOR D = p
	 */
	bv_populate(&xor, 'p', SEG_NR, SEG_SIZE);
	bufvec_xor(&dst, &src, SEG_SIZE * SEG_NR);
	bv_compare(&dst, &xor, SEG_NR, SEG_SIZE);
	bv_free(&src);
	bv_free(&dst);
	bv_free(&xor);
}

const struct m0_test_suite snscm_xform_ut = {
	.ts_name = "snscm_xform-ut",
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
