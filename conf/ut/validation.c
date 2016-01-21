/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Valery V. Vorotyntsev <valery.vorotyntsev@seagate.com>
 * Original creation date: 7-Jan-2016
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "conf/validation.h"
#include "conf/confc.h"
#include "ut/ut.h"

#define XXX_WIP__CONF_VALIDATION 0 /* XXX DELETEME */

#if XXX_WIP__CONF_VALIDATION
static struct m0_confc    g_confc;
static struct m0_sm_group g_grp;
#endif
static char               g_buf[128];

static void test_path(void)
{
#if XXX_WIP__CONF_VALIDATION
	const struct m0_fid bad_profile = M0_FID_TINIT('p', ~0, 9);
	char               *err;

	err = m0_conf_path_validate(cache, g_buf, sizeof g_buf,
				    M0_CONF_ROOT_PROFILES_FID);
	M0_UT_ASSERT(err == NULL);
	err = m0_conf_path_validate(cache, g_buf, sizeof g_buf,
				    M0_CONF_ROOT_PROFILES_FID,
				    &bad_profile);
	M0_UT_ASSERT(m0_streq(err,
			      "No such path component: <70ffffffffffffff:9>"));
	err = m0_conf_path_validate(cache, g_buf, sizeof g_buf,
				    M0_CONF_ROOT_PROFILES_FID, XXX_profile,
				    M0_CONF_PROFILE_FILESYSTEM_FID,
				    M0_CONF_FILESYSTEM_RACKS_FID,
				    M0_CONF_RACK_ENCLS_FID,
				    M0_CONF_ENCLOSURE_CTRLS_FID);
	M0_UT_ASSERT(err == NULL);
#else
	/* M0_UT_ASSERT(!"XXX IMPLEMENTME"); */
#endif
}

static void test_validation(void)
{
	char *err;

	err = m0_conf_validation_error(NULL, g_buf, sizeof g_buf);
	M0_UT_ASSERT(err == NULL);
	/* M0_UT_ASSERT(!"XXX IMPLEMENTME"); */
}

#if XXX_WIP__CONF_VALIDATION
static int conf_validation_ut_init(void)
{
	M0_SET0(&g_grp);
	m0_sm_group_init(&g_grp);
	return m0_confc_init(&g_confc, &g_grp, NULL, NULL, "XXX_confstr");
}

static int conf_validation_ut_fini(void)
{
	m0_confc_fini(&g_confc);
	m0_sm_group_fini(&g_grp);
	return 0;
}
#endif

struct m0_ut_suite conf_validation_ut = {
	.ts_name  = "conf-validation",
#if XXX_WIP__CONF_VALIDATION
	.ts_init  = conf_validation_ut_init,
	.ts_fini  = conf_validation_ut_fini,
#endif
	.ts_tests = {
		{ "path",       test_path },
		{ "validation", test_validation },
		{ NULL, NULL }
	}
};

#undef M0_TRACE_SUBSYSTEM
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
