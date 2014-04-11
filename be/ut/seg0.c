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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 28-Mar-2014
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"
#include "lib/types.h"		/* m0_uint128_eq */
#include "lib/misc.h"		/* M0_BITS */
#include "lib/memory.h"         /* M0_ALLOC_PTR, m0_free */
#include "lib/errno.h"          /* program_invocation_name */
#include "be/ut/helper.h"	/* m0_be_ut_backend */
#include "be/seg.h"
#include "be/seg0.h"
#include "stob/stob.h"
#include "stob/linux.h"
#include "ut/ut.h"
#include "ut/stob.h"		/* m0_ut_stob_linux_get_by_key */

#include <unistd.h>		/* chdir, get_current_dir_name */
#include <stdlib.h>		/* system */

void fake_mkfs(void)
{
	extern char *program_invocation_name;
	char *ut_dir;
	char cmd[512] = {};
	int rc;

	ut_dir = get_current_dir_name();
	rc = chdir("..");
	M0_UT_ASSERT(rc == 0);

	snprintf(cmd, ARRAY_SIZE(cmd), "%s -t be-ut:fake_mkfs -k > "
		 "/dev/null 2>&1", program_invocation_name);
	rc = system(cmd);
	M0_UT_ASSERT(rc == 0);

	rc = chdir(ut_dir);
	M0_UT_ASSERT(rc == 0);

	free(ut_dir);
}

/*
 * // level n
 * m0_be_domain_init(&dom);
 * m0_be_0type_register(&dom, foo);
 * m0_be_0type_register(&dom, bar);
 *
 * // level m (m > n)
 * dom.seg0_stob = stob;
 * m0_be_domain_start(&dom);
 */
static void be_domain_init(struct m0_be_domain *dom, struct m0_stob *seg0_stob)
{
	int rc;
	struct m0_be_domain_cfg cfg;

	m0_be_domain_init(dom);
	m0_be_0type_register(dom, &m0_be_seg0);
	m0_be_0type_register(dom, &m0_be_log0);

	m0_be_ut_backend_cfg_default(&cfg);
	cfg.bc_engine.bec_group_fom_reqh = m0_be_ut_reqh_get();

	dom->bd_seg0_stob = seg0_stob;
	rc = m0_be_domain_start(dom, &cfg);
	M0_UT_ASSERT(rc == 0);
}

static void be_domain_fini(struct m0_be_domain *dom)
{
	m0_be_domain_fini(dom);
	m0_be_ut_reqh_put(dom->bd_cfg.bc_engine.bec_group_fom_reqh);
}

void m0_be_ut_seg0_test(void)
{
	struct m0_stob        *seg0_stob;
	struct m0_be_domain    bedom = {};

	fake_mkfs();

	seg0_stob = m0_ut_stob_linux_get_by_key(1043);
	M0_UT_ASSERT(seg0_stob != NULL);

	be_domain_init(&bedom, seg0_stob);
	be_domain_fini(&bedom);

	m0_ut_stob_put(seg0_stob, false);
}

#undef M0_TRACE_SUBSYSTEM
