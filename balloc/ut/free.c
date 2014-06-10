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
 * Original author: Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 09/02/2010
 */
#include <stdio.h>        /* fprintf */
#include <stdlib.h>       /* srand, rand */
#include <errno.h>
#include <sys/time.h>
#include <err.h>

#include "dtm/dtm.h"      /* m0_dtx */
#include "lib/arith.h"    /* M0_3WAY, m0_uint128 */
#include "lib/misc.h"     /* M0_SET0 */
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/thread.h"
#include "lib/getopts.h"
#include "be/be.h"
#include "ut/ut.h"
#include "balloc/balloc.h"
#include "be/ut/helper.h"

static int be_tx_init_open(struct m0_be_tx *tx,
			   struct m0_be_ut_backend *ut_be,
			   struct m0_be_tx_credit *cred)
{
	int rc;

	m0_be_ut_tx_init(tx, ut_be);
	m0_be_tx_prep(tx, cred);
	m0_be_tx_open(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_ACTIVE,
						M0_BTS_FAILED),
				    M0_TIME_NEVER);
	M0_UT_ASSERT(m0_be_tx_state(tx) == M0_BTS_ACTIVE);

	return rc;
}

int main(int argc, char **argv)
{
	struct m0_balloc	*mero_balloc;
	struct m0_be_ut_backend	 ut_be;
	struct m0_be_ut_seg	 ut_seg;
	struct m0_be_seg	*seg;
	struct m0_be_tx_credit	 cred;
	struct m0_dtx		 dtx;
	struct m0_be_tx		*tx = &dtx.tx_betx;
	struct m0_ext		 ext = { 0 };
	int			 result;

	if (argc != 3) {
		fprintf(stderr, "Usage: start end\n");
		return 1;
	}
	++argv;
	ext.e_start = atoll(argv[0]);
	ext.e_end   = atoll(argv[1]);

	if (m0_ext_is_empty(&ext)) {
		fprintf(stderr, "Invalid extent\n");
		return -EINVAL;
	}

	/* Init BE */
	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, 1ULL << 24);
	seg = ut_seg.bus_seg;

	result = m0_balloc_allocate(0, seg, &mero_balloc);
	M0_ASSERT(result == 0);

	result = mero_balloc->cb_ballroom.ab_ops->bo_init
		(&mero_balloc->cb_ballroom, seg, BALLOC_DEF_BLOCK_SHIFT,
		 BALLOC_DEF_CONTAINER_SIZE, BALLOC_DEF_BLOCKS_PER_GROUP,
		 BALLOC_DEF_RESERVED_GROUPS);

	if (result == 0) {
		cred = M0_BE_TX_CREDIT(0, 0);
		mero_balloc->cb_ballroom.ab_ops->bo_free_credit(
			    &mero_balloc->cb_ballroom, 1, &cred);
		result = be_tx_init_open(tx, &ut_be, &cred);
		M0_ASSERT(result == 0);

		result = mero_balloc->cb_ballroom.ab_ops->bo_free(
			    &mero_balloc->cb_ballroom, &dtx, &ext);
		printf("rc = %d: freed [%08llx,%08llx)\n", result,
			(unsigned long long)ext.e_start,
			(unsigned long long)ext.e_end);

		m0_be_tx_close(tx);
		result = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_DONE),
						M0_TIME_NEVER);
		m0_be_tx_fini(tx);
	}
	M0_ASSERT(result == 0);
	mero_balloc->cb_ballroom.ab_ops->bo_fini(&mero_balloc->cb_ballroom);

	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);
	printf("done\n");
	return 0;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
