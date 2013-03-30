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
 * Original author: Carl Braganza
 * Original creation date: 09/25/2012
 */


#undef M0_ADDB_CT_CREATE_DEFINITION
#undef M0_ADDB_RT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_RT_CREATE_DEFINITION

#include "lib/finject.h"
#include "lib/misc.h" /* M0_SET0 */
#include "lib/semaphore.h"
#include "lib/thread.h"
#include "ut/ut.h"

/* control symbol exposure by including .c files */
#include "addb/addb.c" /* directly pick up internal static symbols */
#include "addb/ut/addb_ut_md.c"
#include "addb/ut/addb_ut_mc.c"
#include "addb/ut/addb_ut_ctx.c"
#include "addb/ut/addb_ut_evmgr_pt.c"
#include "addb/ut/addb_ut_cntr.c"
#ifndef __KERNEL__
#include "addb/ut/addb_ut_stobsink.c"
#include "addb/ut/addb_ut_svc.c"
#endif

/*
 ****************************************************************************
 * Test to validate that m0_node_uuid is initialized.
 ****************************************************************************
 */
static void addb_ut_node_uuid(void)
{
#ifdef __KERNEL__
	/* see utils/linux_kernel/ut.sh.in */
	M0_UT_ASSERT(m0_node_uuid.u_hi == 0x1234567890abcdef);
	M0_UT_ASSERT(m0_node_uuid.u_lo == 0xfedcba0987654321);
#else
	/* see utils/ut.sh.in */
	M0_UT_ASSERT(m0_node_uuid.u_hi == 0xabcdef0123456789);
	M0_UT_ASSERT(m0_node_uuid.u_lo == 0x0123456789abcdef);
#endif
}

static int addb_ut_init(void)
{
	m0_mutex_init(&addb_ut_mc_rs_mutex);
	addb_rec_post_ut_data_enabled = true;
	return 0;
}

static int addb_ut_fini(void)
{
	addb_rec_post_ut_data_enabled = false;
	m0_mutex_fini(&addb_ut_mc_rs_mutex);
	/* restore the global machine for other UTs */
	m0_addb_mc_fini(&m0_addb_gmc);
	m0_addb_mc_init(&m0_addb_gmc);
	return 0;
}

const struct m0_test_suite m0_addb_ut = {
        .ts_name  = "addb-ut",
        .ts_init  = addb_ut_init,
        .ts_fini  = addb_ut_fini,
        .ts_tests = {
		{ "addb-node-uuid",          addb_ut_node_uuid },
		{ "addb-ct",                 addb_ut_ct_test },
		{ "addb-rt",                 addb_ut_rt_test },
		{ "addb-mc",                 addb_ut_mc_test },
		{ "addb-ctx-global-init",    addb_ut_ctx_global_init_test },
		{ "addb-ctx-global-post",    addb_ut_ctx_global_post_test },
		{ "addb-ctx-init",           addb_ut_ctx_init_test },
		{ "addb-ctx-import-export",  addb_ut_ctx_import_export },
		{ "addb-evmgr-pt-config",    addb_ut_evmgr_pt_config_test },
		{ "addb-evmgr-pt-post",      addb_ut_evmgr_pt_post_test },
		{ "addb-cntr",               addb_ut_cntr_test },
#ifndef __KERNEL__
		{ "addb-stobsink-search",    addb_ut_stobsink_search },
		{ "addb-stob-post-retrieve", addb_ut_stob },
		{ "addb-svc",                addb_ut_svc_test },
#endif
		{ NULL, NULL }
        }
};
M0_EXPORTED(m0_addb_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
