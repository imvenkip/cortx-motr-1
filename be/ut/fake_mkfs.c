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
#include "be/ut/helper.h"	/* m0_be_ut_backend */
#include "ut/ut.h"

#define M0_BE_LOG_NAME  "M0_BE:LOG"
#define M0_BE_SEG0_NAME "M0_BE:SEG0"

void m0_be_ut_fake_mkfs(void)
{
	struct m0_be_0type_log_opts *log_opts;
	struct m0_buf               *log_opts_buf;
	struct m0_be_ut_backend      ut_be;
	struct m0_be_tx_credit       credit = {};
	struct m0_sm_group          *grp;
	struct m0_be_seg            *seg;
	struct m0_be_tx              tx;
	int                          rc;

	M0_SET0(&ut_be);
	m0_be_ut_backend_mkfs_init(&ut_be);
	seg = m0_be_domain_seg0_get(&ut_be.but_dom);
	m0_be_ut__seg_allocator_init(seg, &ut_be);
	m0_be_ut_tx_init(&tx, &ut_be);

	grp = m0_be_ut_backend_sm_group_lookup(&ut_be);
	rc = m0_be_ut__seg_dict_create(seg, grp);
	M0_UT_ASSERT(rc == 0);

	/* 1) log */
	m0_be_seg_dict_insert_credit(seg, M0_BE_LOG_NAME, &credit);
	M0_BE_ALLOC_CREDIT_PTR(log_opts_buf, seg, &credit);
	M0_BE_ALLOC_CREDIT_PTR(log_opts, seg, &credit);
	/* 2) seg0 */
	m0_be_seg_dict_insert_credit(seg, M0_BE_SEG0_NAME, &credit);
	/* end */

	m0_be_tx_prep(&tx, &credit);
	m0_be_tx_open_sync(&tx);

	/* 1) log */
	M0_BE_ALLOC_PTR_SYNC(log_opts_buf, seg, &tx);
	M0_BE_ALLOC_PTR_SYNC(log_opts, seg, &tx);
	log_opts->lo_size            = 1 << 27;
	log_opts->lo_stob_id.si_bits = M0_UINT128(0, 42);
	*log_opts_buf = M0_BUF_INIT_PTR(log_opts);
	M0_BE_TX_CAPTURE_PTR(seg, &tx, log_opts);
	M0_BE_TX_CAPTURE_PTR(seg, &tx, log_opts_buf);

	rc = m0_be_seg_dict_insert(seg, &tx, M0_BE_LOG_NAME, log_opts_buf);
	M0_UT_ASSERT(rc == 0);

	/* 2) seg0 */
	/* There's no seg0 options yet so passing NULL-pointer here */
	rc = m0_be_seg_dict_insert(seg, &tx, M0_BE_SEG0_NAME, NULL);
	M0_UT_ASSERT(rc == 0);

	/* end */

	m0_be_tx_close_sync(&tx);
	m0_be_tx_fini(&tx);
	m0_be_ut_backend_fini(&ut_be);
}

#undef M0_TRACE_SUBSYSTEM
