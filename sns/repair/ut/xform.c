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

#include "lib/processor.h"
#include "lib/ut.h"
#include "lib/vec.h"
#include "colibri/init.h"
#include "reqh/reqh.h"
#include "sns/repair/cp.h"
#include "sns/repair/ag.h"

enum {
	NR = 255,
	LOCAL_CP_SINGLE_NR = 1,
	LOCAL_CP_MULTIPLE_NR = 20000,
};

static struct c2_sns_repair_ag s_sns_ag;
static struct c2_cm_cp         s_cp;
static struct c2_bufvec        s_bv;

static struct c2_sns_repair_ag sns_ag;
static struct c2_cm_cp         cp[LOCAL_CP_MULTIPLE_NR];
static struct c2_bufvec        bv[LOCAL_CP_MULTIPLE_NR];

static struct c2_reqh	       reqh;

static int next_phase(struct c2_cm_cp *cp)
{
	/**
	 * Typically, the next phase after CC_XFORM is CCP_WRITE.
	 * i.e. after transformation, the copy packet gets written to the
	 * device. Hence, mimic this phase change.
	 * @todo This function can be removed once actual next phase function
	 * is implemented.
	 */
	cp->c_fom.fo_phase = CCP_WRITE;
	return C2_FSO_AGAIN;
}

const struct c2_cm_cp_ops cp_ops = {
        .co_phase    = &next_phase,
	.co_xform    = &repair_cp_xform,
};

static uint64_t local_cp_single_nr(struct c2_cm_aggr_group *ag)
{
	return LOCAL_CP_SINGLE_NR;
}

static const struct c2_cm_aggr_group_ops group_single_ops = {
        .cago_local_cp_nr = &local_cp_single_nr,
};

static uint64_t local_cp_multiple_nr(struct c2_cm_aggr_group *ag)
{
	return LOCAL_CP_MULTIPLE_NR;
}

static const struct c2_cm_aggr_group_ops group_multiple_ops = {
        .cago_local_cp_nr = &local_cp_multiple_nr,
};

static void populate_bv(struct c2_bufvec *b, int data)
{
	int i;

	C2_UT_ASSERT(b != NULL);
        C2_UT_ASSERT(c2_bufvec_alloc(b, NR, C2_SEG_SIZE) == 0);
        C2_UT_ASSERT(b->ov_vec.v_nr == NR);
        for (i = 0; i < NR; ++i) {
                C2_UT_ASSERT(b->ov_vec.v_count[i] == C2_SEG_SIZE);
                C2_UT_ASSERT(b->ov_buf[i] != NULL);
		memset(b->ov_buf[i], data, C2_SEG_SIZE);
        }
}

static void free_bv(struct c2_bufvec *b)
{
        c2_bufvec_free(b);
        C2_UT_ASSERT(b->ov_vec.v_nr == 0);
        C2_UT_ASSERT(b->ov_buf == NULL);
}

static size_t dummy_fom_locality(const struct c2_fom *fom)
{
        return 0;
}

static int dummy_fom_state(struct c2_fom *fom)
{
	struct c2_cm_cp *cp;

	cp = container_of(fom, struct c2_cm_cp, c_fom);
	switch (fom->fo_phase) {
	case C2_FOPH_INIT:
		cp->c_fom.fo_phase = CCP_XFORM;
		return cp->c_ops->co_xform(cp);
	case CCP_FINI:
		fom->fo_phase = C2_FOPH_FINISH;
                return C2_FSO_WAIT;
	case CCP_WRITE:
		fom->fo_phase = C2_FOPH_FINISH;
                return C2_FSO_WAIT;
	default:
		C2_IMPOSSIBLE("Bad State");
	}
}

static void dummy_fom_fini(struct c2_fom *fom)
{
	struct c2_cm_cp *cp;

	cp = container_of(fom, struct c2_cm_cp, c_fom);
	cp->c_fom.fo_phase = C2_FOPH_FINISH;
	free_bv(cp->c_data);
	c2_cm_cp_fini(cp);
}

static const struct c2_fom_ops cp_fom_ops = {
        .fo_fini          = dummy_fom_fini,
        .fo_state         = dummy_fom_state,
        .fo_home_locality = dummy_fom_locality
};

static void cp_prepare(struct c2_cm_cp *cp, struct c2_bufvec *bv,
		       struct c2_sns_repair_ag *sns_ag,
		       int data)
{
	C2_UT_ASSERT(cp != NULL);
	C2_UT_ASSERT(bv != NULL);
	C2_UT_ASSERT(sns_ag != NULL);

	populate_bv(bv, data);
	cp->c_ag = &sns_ag->sag_base;
	c2_cm_cp_init(cp, &cp_ops, bv);
	cp->c_fom.fo_ops = &cp_fom_ops;
	cp->c_fom.fo_fop = (void *)1;
}

static void test_single_cp(void)
{
	cp_prepare(&s_cp, &s_bv, &s_sns_ag, 1);
	s_cp.c_ag->cag_ops = &group_single_ops;
	c2_fom_queue(&s_cp.c_fom, &reqh);
}

static void test_multiple_cp(void)
{
	int i;

	for (i = 0; i < LOCAL_CP_MULTIPLE_NR; ++i) {
		cp_prepare(&cp[i], &bv[i], &sns_ag, i);
		cp[i].c_ag->cag_ops = &group_multiple_ops;
		c2_fom_queue(&cp[i].c_fom, &reqh);
	}
}

static int xform_init(void)
{
	int rc;

	if (!c2_processor_is_initialized()) {
		rc = c2_processors_init();
                C2_ASSERT(rc == 0);
        }

	c2_reqh_init(&reqh, NULL, (void*)1, (void*)1, (void*)1);
	return 0;
}

static int xform_fini(void)
{
	c2_reqh_fini(&reqh);
        if (c2_processor_is_initialized())
                c2_processors_fini();
	return 0;
}

const struct c2_test_suite snsrepair_xform_ut = {
        .ts_name = "snsrepair_xform-ut",
        .ts_init = &xform_init,
        .ts_fini = &xform_fini,
        .ts_tests = {
                { "single_cp_passthrough", test_single_cp },
                { "multiple_cp_bufvec_xor", test_multiple_cp },
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
