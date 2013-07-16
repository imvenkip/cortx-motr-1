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
 * Original creation date: 12-Jun-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "be/tx_fom.h"
#include "lib/types.h"     /* m0_uint128_eq */
#include "lib/misc.h"      /* M0_BITS, M0_IN */
#include "be/ut/helper.h"
#include "ut/ut.h"

void m0_be_ut_tx_simple(void)
{
	struct m0_be_tx_credit cred;
	struct m0_be_ut_h      h;
	struct m0_uint128     *p;
	struct m0_uint128      p_save;
	struct m0_be_op        op;
	struct m0_be_tx        tx = {};
	int                    rc;

	M0_ENTRY();
	/*
	 * Init BE, BE IO, credits
	 */
	m0_be_ut_h_init(&h);

	m0_be_op_init(&op);
	m0_be_tx_credit_init(&cred);

	/*
	 * Init transaction and its credits
	 */
	m0_be_ut_h_tx_init(&tx, &h);
	m0_be_allocator_credit(h.buh_allocator, M0_BAO_ALLOC, sizeof *p, 0,
			       &cred);
	m0_be_tx_credit_add(&cred, &M0_BE_TX_CREDIT(1, sizeof *p));

	m0_sm_group_lock(&ut__txs_sm_group);

	m0_be_tx_prep(&tx, &cred);

	/* Open transaction, allocate, dirty and capture region. */
	m0_be_tx_open(&tx);
	rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_ACTIVE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_LOG(M0_DEBUG, "Transaction has reached M0_BTS_ACTIVE");

	p = m0_be_alloc(h.buh_allocator, &tx, &op, sizeof *p, 0);
	M0_UT_ASSERT(p != NULL);
	M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
						 M0_BOS_FAILURE)));
	p->u_hi = 0xdeadd00d8badf00d;
	p->u_lo = 0x5ca1ab1e7e1eca57;
	p_save = *p;
	M0_BE_TX_CAPTURE_PTR(&h.buh_seg, &tx, p);
	p->u_hi = 0;
	p->u_lo = 0;
	M0_UT_ASSERT(!m0_uint128_eq(p, &p_save));

	m0_be_tx_close(&tx); /* Make things persistent. */

	rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_PLACED), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_LOG(M0_DEBUG, "Transaction has reached M0_BTS_PLACED");

	/* XXX TODO: m0_be_tx_stable(tx) */
	/*
	m0_be_tx_stable(&tx);
	m0_be_tx_fini(&tx);
	*/

	m0_sm_group_unlock(&ut__txs_sm_group);

	/*
	 * Reload segment and check data
	 */
	m0_be_ut_h_seg_reload(&h);
	M0_UT_ASSERT(m0_uint128_eq(p, &p_save));
	M0_LOG(M0_DEBUG, "Segment data is reloaded properly");

	/** XXX TODO m0_be_free() */
	m0_be_ut_h_fini(&h);

	M0_LEAVE();
}

struct complex {
	float real;
	float imag;
};

struct x {
	struct m0_be_tx        tx;
	struct m0_be_tx_credit cred;
	m0_bcount_t            size;
	void                  *data;
	const union {
		struct m0_uint128 u128;
		struct complex    complex;
	} captured;
};

enum { SHIFT = 0 };

static void
transact(struct x *x, struct m0_be_allocator *allocator, struct m0_be_seg *seg)
{
	struct m0_be_op op;
	int             rc;

	m0_be_op_init(&op);
	m0_be_tx_prep(&x->tx, &x->cred);

	/* Open transaction synchronously. */
	m0_be_tx_open(&x->tx);
	rc = m0_be_tx_timedwait(&x->tx, M0_BITS(M0_BTS_ACTIVE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);

	/* Allocate memory. */
	x->data = m0_be_alloc(allocator, &x->tx, &op, x->size, SHIFT);
	M0_UT_ASSERT(x->data != NULL);
	M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
						 M0_BOS_FAILURE)));

	/* Dirty the memory. */
	M0_CASSERT(sizeof(struct m0_uint128) != sizeof(int));
	M0_CASSERT(sizeof(struct m0_uint128) != sizeof(struct complex));
	M0_ASSERT(M0_IN(x->size, (sizeof(struct m0_uint128),
				  sizeof(struct complex))));
	memcpy(x->data, &x->captured, x->size);

	/* Capture dirty memory. */
	m0_be_tx_capture(&x->tx, &M0_BE_REG(seg, x->size, x->data));

	/* Simulate amnesia. (The data will be loaded from the segment.) */
	memset(x->data, 0, x->size);

	m0_be_tx_close(&x->tx);
	m0_be_op_fini(&op);
}

void m0_be_ut_tx_several(void)
{
	struct m0_be_ut_h h;
	size_t            i;
	struct x         *x;
	struct x          xs[] = {
		{
			.size          = sizeof(struct m0_uint128),
			.captured.u128 = M0_UINT128(0xdeadd00d8badf00d,
						    0x5ca1ab1e7e1eca57)
		},
		{
			.size             = sizeof(struct complex),
			.captured.complex = { .real = 1.8, .imag = 0.4 }
		}
	};

	m0_be_ut_h_init(&h);

	for (i = 0; i < ARRAY_SIZE(xs); ++i) {
		x = &xs[i];
		m0_be_ut_h_tx_init(&x->tx, &h);
		m0_be_tx_credit_init(&x->cred);
		m0_be_allocator_credit(h.buh_allocator, M0_BAO_ALLOC, x->size,
				       SHIFT, &x->cred);
		m0_be_tx_credit_add(&x->cred, &M0_BE_TX_CREDIT(1, x->size));
	}

	m0_sm_group_lock(&ut__txs_sm_group);

	for (i = 0; i < ARRAY_SIZE(xs); ++i)
		transact(&xs[i], h.buh_allocator, &h.buh_seg);

	/* Wait for transactions to become persistent. */
	for (i = 0; i < ARRAY_SIZE(xs); ++i) {
		int rc = m0_be_tx_timedwait(&xs[i].tx, M0_BITS(M0_BTS_PLACED),
					    M0_TIME_NEVER);
		M0_UT_ASSERT(rc == 0);
	}

#if 0 /* XXX TODO */
	m0_be_tx_stable(&x->tx);
	m0_be_tx_fini(&x->tx);
	m0_be_free(..., x->data);
#endif
	m0_sm_group_unlock(&ut__txs_sm_group);

	/* Reload the segment. */
	m0_be_ut_h_seg_reload(&h);

	/* Validate the data. */
	for (i = 0; i < ARRAY_SIZE(xs); ++i) {
		x = &xs[i];
		M0_UT_ASSERT(memcmp(x->data, &x->captured, x->size) == 0);
	}

	m0_be_ut_h_fini(&h);
}

#undef M0_TRACE_SUBSYSTEM
