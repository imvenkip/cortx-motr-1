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

#include "be/tx_group_fom.h"
#include "lib/types.h"     /* m0_uint128_eq */
#include "lib/misc.h"      /* M0_BITS, M0_IN */
#include "be/ut/helper.h"
#include "ut/ut.h"

static void be_ut_tx_test(size_t nr);

static void be_ut_tx_alloc_init(void **alloc, struct m0_be_seg *seg)
{
	*alloc = seg->bs_addr + seg->bs_size / 2;
}

static void be_ut_tx_alloc_fini(void **alloc)
{
	*alloc = NULL;
}

static void *be_ut_tx_alloc(void **alloc, m0_bcount_t size)
{
	void *ptr = *alloc;

	*alloc += size;
	return ptr;
}

void m0_be_ut_tx_usecase_success(void)
{
	struct m0_be_ut_backend	 ut_be;
	struct m0_be_ut_seg	 ut_seg;
	struct m0_be_seg	*seg = &ut_seg.bus_seg;
	struct m0_be_tx		 tx_;
	struct m0_be_tx		*tx = &tx_;
	uint64_t		*data;
	struct m0_be_tx_credit	 credit;

	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, 1 << 20);

	data = (uint64_t *) ((char *) seg->bs_addr + (1 << 16));
	*data = 0x101;
	credit = M0_BE_TX_CREDIT_TYPE(uint64_t);

	m0_be_ut_backend_tx_init(&ut_be, tx);
	M0_UT_ASSERT(m0_be_tx_state(tx) == M0_BTS_PREPARE);
	M0_UT_ASSERT(tx->t_sm.sm_rc == 0);

	m0_be_tx_prep(tx, &credit);
	M0_UT_ASSERT(m0_be_tx_state(tx) == M0_BTS_PREPARE);
	M0_UT_ASSERT(tx->t_sm.sm_rc == 0);

	m0_be_tx_open(tx);
	m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_ACTIVE, M0_BTS_FAILED),
			   M0_TIME_NEVER);
	M0_UT_ASSERT(m0_be_tx_state(tx) == M0_BTS_ACTIVE);
	M0_UT_ASSERT(tx->t_sm.sm_rc == 0);

	m0_be_tx_capture(tx, &M0_BE_REG_PTR(seg, data));
	m0_be_tx_close(tx);
	m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_DONE), M0_TIME_NEVER);
	m0_be_tx_fini(tx);

	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);
}

void m0_be_ut_tx_usecase_failure(void)
{
	struct m0_be_ut_backend	 ut_be;
	struct m0_be_tx		 tx_;
	struct m0_be_tx		*tx = &tx_;

	m0_be_ut_backend_init(&ut_be);

	m0_be_ut_backend_tx_init(&ut_be, tx);
	M0_UT_ASSERT(m0_be_tx_state(tx) == M0_BTS_PREPARE);
	M0_UT_ASSERT(tx->t_sm.sm_rc == 0);

	m0_be_tx_prep(tx, &M0_BE_TX_CREDIT(1ULL << 20, 1ULL << 25));
	M0_UT_ASSERT(m0_be_tx_state(tx) == M0_BTS_PREPARE);
	M0_UT_ASSERT(tx->t_sm.sm_rc == 0);

	m0_be_tx_open(tx);
	m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_ACTIVE, M0_BTS_FAILED),
			   M0_TIME_NEVER);
	M0_UT_ASSERT(m0_be_tx_state(tx) == M0_BTS_FAILED);
	M0_UT_ASSERT(tx->t_sm.sm_rc != 0);

	m0_be_tx_fini(tx);

	m0_be_ut_backend_fini(&ut_be);
}

void m0_be_ut_tx_single(void)
{
	be_ut_tx_test(1);
}

void m0_be_ut_tx_several(void)
{
	be_ut_tx_test(2);
}

struct be_ut_complex {
	uint32_t real;
	uint32_t imag;
};

struct be_ut_tx_x {
	struct m0_be_tx        tx;
	struct m0_be_tx_credit cred;
	m0_bcount_t            size;
	void                  *data;
	const union {
		struct m0_uint128 u128;
		struct be_ut_complex    be_ut_complex;
	} captured;
};

enum { SHIFT = 0 };

static void be_ut_transact(struct be_ut_tx_x *x,
			   struct m0_be_seg *seg,
			   void **alloc)
{
	int rc;

	m0_be_tx_prep(&x->tx, &x->cred);

	/* Open transaction synchronously. */
	m0_be_tx_open(&x->tx);
	rc = m0_be_tx_timedwait(&x->tx, M0_BITS(M0_BTS_ACTIVE, M0_BTS_FAILED),
				M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);

	x->data = be_ut_tx_alloc(alloc, x->size);

	/* Dirty the memory. */
	M0_CASSERT(sizeof(struct m0_uint128) != sizeof(int));
	M0_CASSERT(sizeof(struct m0_uint128) != sizeof(struct be_ut_complex));
	M0_ASSERT(M0_IN(x->size, (sizeof(struct m0_uint128),
				  sizeof(struct be_ut_complex))));
	memcpy(x->data, &x->captured, x->size);

	/* Capture dirty memory. */
	m0_be_tx_capture(&x->tx, &M0_BE_REG(seg, x->size, x->data));

	/* Simulate amnesia. (The data will be loaded from the segment.) */
	memset(x->data, 0, x->size);

	m0_be_tx_close(&x->tx);
}

/**
 * @param nr  Number of transactions to use.
 */
static void be_ut_tx_test(size_t nr)
{
	struct m0_be_ut_backend	 ut_be;
	struct m0_be_ut_seg	 ut_seg;
	void			*alloc;
	struct be_ut_tx_x	*x;
	struct be_ut_tx_x	 xs[] = {
		{
			.size          = sizeof(struct m0_uint128),
			.captured.u128 = M0_UINT128(0xdeadd00d8badf00d,
						    0x5ca1ab1e7e1eca57)
		},
		{
			.size             = sizeof(struct be_ut_complex),
			.captured.be_ut_complex = { .real = 18, .imag = 04 }
		},
		{ .size = 0 } /* terminator */
	};

	M0_PRE(0 < nr && nr < ARRAY_SIZE(xs));
	xs[nr].size = 0;

	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, 1 << 20);
	be_ut_tx_alloc_init(&alloc, &ut_seg.bus_seg);

	for (x = xs; x->size != 0; ++x) {
		m0_be_ut_backend_tx_init(&ut_be, &x->tx);
		m0_be_tx_get(&x->tx);
		m0_be_tx_credit_init(&x->cred);
		m0_be_tx_credit_add(&x->cred, &M0_BE_TX_CREDIT(1, x->size));
	}

	for (x = xs; x->size != 0; ++x)
		be_ut_transact(x, &ut_seg.bus_seg, &alloc);

	/* Wait for transactions to become persistent. */
	for (x = xs; x->size != 0; ++x) {
		int rc = m0_be_tx_timedwait(&x->tx, M0_BITS(M0_BTS_PLACED),
					    M0_TIME_NEVER);
		M0_UT_ASSERT(rc == 0);
		m0_be_tx_put(&x->tx);
	}

	/* Reload the segment. */
	m0_be_ut_seg_reload(&ut_seg);

	/* Validate the data. */
	for (x = xs; x->size != 0; ++x)
		M0_UT_ASSERT(memcmp(x->data, &x->captured, x->size) == 0);

	for (x = xs; x->size != 0; ++x) {
		int rc = m0_be_tx_timedwait(&x->tx, M0_BITS(M0_BTS_DONE),
					    M0_TIME_NEVER);
		M0_UT_ASSERT(rc == 0);
		m0_be_tx_fini(&x->tx);
	}

	be_ut_tx_alloc_fini(&alloc);
	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);

}

#undef M0_TRACE_SUBSYSTEM
