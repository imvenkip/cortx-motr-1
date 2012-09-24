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
#include "reqh/reqh.h"
#include "cm/cp.h"
#include "cm/cp.c"
#include "sns/repair/cp.h"
#include "cm/ag.h"

static struct c2_sns_repair_cp sns_cp;
static struct c2_reqh  reqh;
static struct c2_cm_aggr_group ag;
static struct c2_bufvec bv;

static void dummy_cp_fom_fini(struct c2_fom *fom)
{
	struct c2_cm_cp *cp = bob_of(fom, struct c2_cm_cp, c_fom, &cp_bob);
	c2_cm_cp_fini(cp);
}

/**
 *  * Over-ridden copy packet FOM ops.
 *   */
static struct c2_fom_ops dummy_cp_fom_ops = {
        .fo_fini          = dummy_cp_fom_fini,
        .fo_tick          = cp_fom_tick,
        .fo_home_locality = cp_fom_locality
};

static void cp_init_fini(void)
{
	struct c2_cm_cp cp;

	cp = sns_cp.rc_base;
	c2_cm_cp_init(&cp);
        cp.c_ag = &ag;
        /** Required to pass the fom invariant */
        cp.c_fom.fo_fop = (void *)1;
	cp.c_data = &bv;
	cp.c_ops = &c2_sns_repair_cp_ops;
	cp.c_fom.fo_ops = &dummy_cp_fom_ops;
	c2_fom_queue(&cp.c_fom, &reqh);
        /**
         * Wait until all the foms in the request handler locality runq are
         * processed. This is required for further validity checks.
         */
        c2_reqh_shutdown_wait(&reqh);
}

/**
 * Initialise the request handler since copy packet fom has to be tested using
 * request handler infrastructure.
 */
static int cm_cp_init(void)
{
        c2_reqh_init(&reqh, NULL, (void*)1, (void*)1, (void*)1);
        return 0;
}

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
                { "cp-init-fini",cp_init_fini },
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
