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

void m0_be_ut_seg0_test(void)
{
	struct m0_stob             *seg0_stob;
	struct m0_be_ut_backend     ut_be = {};
	struct m0_be_tx_credit      credit = {};
	struct m0_be_tx             tx = {};
	struct m0_be_0type_seg_opts seg_opts;
	struct m0_buf               data = M0_BUF_INIT_PTR(&seg_opts);
	struct m0_be_seg           *seg;
	char seg_id[256];
	int rc;

	fake_mkfs();

	seg0_stob = m0_ut_stob_linux_get_by_key(1043);
	M0_UT_ASSERT(seg0_stob != NULL);

	m0_be_ut_backend_init_normal(&ut_be, seg0_stob);

	m0_be_ut_tx_init(&tx, &ut_be);

	/* take a seg from existing segments with address range which
	 * is more than seg0 addr range */
	seg = m0_be_domain_seg(&ut_be.but_dom, (void*)(BE_UT_SEG_START_ADDR +
						       (8ULL<<24)));
	M0_UT_ASSERT(seg != NULL);
	M0_UT_ASSERT(seg != m0_be_domain_seg0_get(&ut_be.but_dom));

	snprintf(seg_id, ARRAY_SIZE(seg_id), "%08lu", seg->bs_id);
	seg_opts.so_stob_fid = seg->bs_stob->so_fid;

	m0_be_0type_del_credit(&ut_be.but_dom, &m0_be_seg0,
			       seg_id, &data, &credit);
	m0_be_0type_add_credit(&ut_be.but_dom, &m0_be_seg0,
			       seg_id, &data, &credit);

	m0_be_tx_prep(&tx, &credit);
	m0_be_tx_exclusive_open_sync(&tx);

	rc = m0_be_0type_del(&m0_be_seg0, &ut_be.but_dom, &tx, seg_id, NULL);
	M0_UT_ASSERT(rc == 0);

	rc = m0_be_0type_add(&m0_be_seg0, &ut_be.but_dom, &tx, seg_id, &data);
	M0_UT_ASSERT(rc == 0);

	m0_be_tx_close_sync(&tx);
	m0_be_tx_fini(&tx);

	m0_be_ut_backend_fini(&ut_be);

	m0_ut_stob_put(seg0_stob, false);
}

#undef M0_TRACE_SUBSYSTEM
