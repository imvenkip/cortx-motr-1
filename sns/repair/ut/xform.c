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

#include "lib/ut.h"
#include "lib/vec.h"

#include "sns/repair/cp.h"
#include "sns/repair/ag.h"

struct c2_sns_repair_ag sns_ag;
struct c2_cm_cp         cp;
struct c2_bufvec        bv;

enum {
	NR = 255,
	LOCAL_CP_SINGLE_NR = 1,
	LOCAL_CP_DOUBLE_NR = 2,
	LOCAL_CP_MULTIPLE_NR = 10,
};

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
};

static uint64_t local_cp_single_nr(struct c2_cm_aggr_group *ag)
{
	return LOCAL_CP_SINGLE_NR;
}

static const struct c2_cm_aggr_group_ops group_single_ops = {
        .cago_local_cp_nr = &local_cp_single_nr,
};

static uint64_t local_cp_double_nr(struct c2_cm_aggr_group *ag)
{
	return LOCAL_CP_DOUBLE_NR;
}

static const struct c2_cm_aggr_group_ops group_double_ops = {
        .cago_local_cp_nr = &local_cp_double_nr,
};

static uint64_t local_cp_multiple_nr(struct c2_cm_aggr_group *ag)
{
	return LOCAL_CP_MULTIPLE_NR;
}

static const struct c2_cm_aggr_group_ops group_multiple_ops = {
        .cago_local_cp_nr = &local_cp_multiple_nr,
};

static void populate_bv()
{
	int i;

        C2_UT_ASSERT(c2_bufvec_alloc(&bv, NR, C2_SEG_SIZE) == 0);
        C2_UT_ASSERT(bv.ov_vec.v_nr == NR);
        for (i = 0; i < NR; ++i) {
                C2_UT_ASSERT(bv.ov_vec.v_count[i] == C2_SEG_SIZE);
                C2_UT_ASSERT(bv.ov_buf[i] != NULL);
		memset(bv.ov_buf[i], i, C2_SEG_SIZE);
        }
}

static void free_bv()
{
        c2_bufvec_free(&bv);
        C2_UT_ASSERT(bv.ov_vec.v_nr == 0);
        C2_UT_ASSERT(bv.ov_buf == NULL);
}

static void test_single_cp(void)
{
	populate_bv();
	cp.c_data = &bv;
	cp.c_fom.fo_phase = CCP_XFORM;
	cp.c_ops = &cp_ops;
	cp.c_ag = &sns_ag.sag_base;
	cp.c_ag->cag_ops = &group_single_ops;
	C2_UT_ASSERT(repair_cp_xform(&cp) == C2_FSO_AGAIN);
	C2_UT_ASSERT(cp.c_fom.fo_phase == CCP_WRITE);
	free_bv();
}

const struct c2_test_suite snsrepair_xform_ut = {
        .ts_name = "snsrepair_xform-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "single_cp_passthrough", test_single_cp },
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
