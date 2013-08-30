/* -*- C -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 13-Aug-2013
 */

/**
 * @addtogroup ut
 *
 * @{
 */

#include "ut/be.h"
#include "ut/ut.h"         /* M0_UT_ASSERT */
#include "be/ut/helper.h"  /* m0_be_ut_backend */
#include "lib/misc.h"      /* M0_BITS */

M0_INTERNAL void
m0_ut_backend_init(struct m0_be_ut_backend *be, struct m0_be_ut_seg *seg)
{
	m0_be_ut_backend_init(be);
	m0_be_ut_seg_init(seg, be, 1 << 20 /* 1 MB */);
	m0_be_ut_seg_allocator_init(seg, be);
}

M0_INTERNAL void
m0_ut_backend_fini(struct m0_be_ut_backend *be, struct m0_be_ut_seg *seg)
{
	m0_be_ut_seg_allocator_fini(seg, be);
	m0_be_ut_seg_fini(seg);
	m0_be_ut_backend_fini(be);
}

M0_INTERNAL void m0_ut_be_tx_begin(struct m0_be_tx *tx,
				   struct m0_be_ut_backend *ut_be,
				   struct m0_be_tx_credit *cred)
{
	int rc;

	m0_be_ut_tx_init(tx, ut_be);
	m0_be_tx_prep(tx, cred);
	m0_be_tx_open(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_ACTIVE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
}

M0_INTERNAL void m0_ut_be_tx_end(struct m0_be_tx *tx)
{
	int rc;

	m0_be_tx_close(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_DONE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	m0_be_tx_fini(tx);
}

M0_INTERNAL void *m0_ut_be_alloc(m0_bcount_t size, struct m0_be_seg *seg,
				 struct m0_be_ut_backend *ut_be)
{
	enum { SHIFT = 0 };
	M0_BE_TX_CREDIT(cred);
	struct m0_be_tx tx;
	void           *ptr;

	m0_be_allocator_credit(&seg->bs_allocator, M0_BAO_ALLOC, size, SHIFT,
			       &cred);
	m0_ut_be_tx_begin(&tx, ut_be, &cred);
	M0_BE_OP_SYNC(
		op,
		ptr = m0_be_alloc(&seg->bs_allocator, &tx, &op, size, SHIFT));
	m0_ut_be_tx_end(&tx);
	return ptr;
}

M0_INTERNAL void m0_ut_be_free(void *ptr, m0_bcount_t size,
			       struct m0_be_seg *seg,
			       struct m0_be_ut_backend *ut_be)
{
	enum { SHIFT = 0 };
	M0_BE_TX_CREDIT(cred);
	struct m0_be_tx tx;

	m0_be_allocator_credit(&seg->bs_allocator, M0_BAO_FREE, size, SHIFT,
			       &cred);
	m0_ut_be_tx_begin(&tx, ut_be, &cred);
	M0_BE_OP_SYNC(op, m0_be_free(&seg->bs_allocator, &tx, &op, ptr));
	m0_ut_be_tx_end(&tx);
}

/** @} end of be group */

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
