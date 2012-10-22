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
 * Original creation date: 10/22/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "reqh/reqh.h"
#include "sns/repair/ut/cp_common.h"


static struct c2_reqh reqh;


#if 0
/* Global structures for single copy packet test. */
static struct c2_sns_repair_ag sag;
static struct c2_cm_cp         cp;
static struct c2_bufvec        bv;

static uint64_t cp_get_nr(struct c2_cm_aggr_group *ag)
{
        return CP_NR;
}

static const struct c2_cm_aggr_group_ops ag_ops = {
        .cago_local_cp_nr = &cp_get_nr,
};

#endif

/*
 * Initialises the request handler since copy packet fom has to be tested using
 * request handler infrastructure.
 */
static int storage_init(void)
{
	int rc;

        rc = c2_reqh_init(&reqh, NULL, (void*)1, (void*)1, (void*)1, (void*)1);
	C2_ASSERT(rc == 0);

        return 0;
}

static int storage_fini(void)
{
        c2_reqh_fini(&reqh);

        return 0;
}

static void test_read(void)
{
}

const struct c2_test_suite snsrepair_storage_ut = {
        .ts_name = "snsrepair_storage-ut",
        .ts_init = &storage_init,
        .ts_fini = &storage_fini,
        .ts_tests = {
                { "test_read", test_read },
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
