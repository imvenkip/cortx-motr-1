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

#include "lib/types.h"		/* m0_uint128_eq */
#include "lib/misc.h"		/* M0_BITS */

#include "ut/ut.h"

#include "be/ut/helper.h"	/* m0_be_ut_backend */

#include <stdlib.h>		/* rand_r */

void m0_be_ut_tx_usecase_success(void)
{
	struct m0_be_ut_backend ut_be;
	struct m0_be_ut_seg     ut_seg;
	struct m0_be_seg       *seg = &ut_seg.bus_seg;
	struct m0_be_tx_credit  credit = M0_BE_TX_CREDIT_TYPE(uint64_t);
	struct m0_be_tx         tx;
	uint64_t               *data;
	int                     rc;

	M0_SET0(&ut_be);
	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, &ut_be, 1 << 20);

	m0_be_ut_tx_init(&tx, &ut_be);

	m0_be_tx_prep(&tx, &credit);

	/* m0_be_tx_open_sync() can be used in UT */
	m0_be_tx_open(&tx);
	rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_ACTIVE, M0_BTS_FAILED),
				M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);

	data = (uint64_t *) (seg->bs_addr + seg->bs_reserved);
	*data = 0x101;
	m0_be_tx_capture(&tx, &M0_BE_REG_PTR(seg, data));

	/* m0_be_tx_close_sync() can be used in UT */
	m0_be_tx_close(&tx);
	rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_DONE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	m0_be_tx_fini(&tx);

	m0_be_ut_seg_check_persistence(&ut_seg);
	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);
}

void m0_be_ut_tx_usecase_failure(void)
{
	struct m0_be_ut_backend ut_be;
	struct m0_be_tx         tx;
	int                     rc;

	M0_SET0(&ut_be);
	m0_be_ut_backend_init(&ut_be);

	m0_be_ut_tx_init(&tx, &ut_be);

	m0_be_tx_prep(&tx, &m0_be_tx_credit_invalid);

	m0_be_tx_open(&tx);
	rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_ACTIVE, M0_BTS_FAILED),
				M0_TIME_NEVER);
	M0_UT_ASSERT(rc != 0);

	m0_be_tx_fini(&tx);

	m0_be_ut_backend_fini(&ut_be);
}

static void be_ut_tx_test(size_t nr);

static void be_ut_tx_alloc_init(void **alloc, struct m0_be_seg *seg)
{
	*alloc = seg->bs_addr + seg->bs_reserved;
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

void m0_be_ut_tx_states(void)
{
	struct m0_be_ut_backend ut_be;
	struct m0_be_tx_credit  credit = M0_BE_TX_CREDIT_TYPE(uint64_t);
	struct m0_be_ut_seg     ut_seg;
	struct m0_be_seg       *seg = &ut_seg.bus_seg;
	struct m0_be_tx         tx;
	uint64_t               *data;
	int                     rc;

	M0_SET0(&ut_be);
	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, &ut_be, 1 << 20);

	/* test success path */
	m0_be_ut_tx_init(&tx, &ut_be);
	M0_UT_ASSERT(tx.t_sm.sm_rc == 0);
	M0_UT_ASSERT(m0_be_tx_state(&tx) == M0_BTS_PREPARE);

	m0_be_tx_prep(&tx, &credit);
	M0_UT_ASSERT(tx.t_sm.sm_rc == 0);
	M0_UT_ASSERT(m0_be_tx_state(&tx) == M0_BTS_PREPARE);

	/* to check M0_BTS_PLACED state */
	m0_be_tx_get(&tx);

	rc = m0_be_tx_open_sync(&tx);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_be_tx_state(&tx) == M0_BTS_ACTIVE);

	data = (uint64_t *) (seg->bs_addr + seg->bs_reserved);
	*data = 0x101;
	m0_be_tx_capture(&tx, &M0_BE_REG_PTR(seg, data));

	m0_be_tx_close(&tx);
	rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_PLACED), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_be_tx_state(&tx) == M0_BTS_PLACED);

	m0_be_tx_put(&tx);
	rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_DONE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_be_tx_state(&tx) == M0_BTS_DONE);

	m0_be_tx_fini(&tx);

	/* test failure path */
	m0_be_ut_tx_init(&tx, &ut_be);
	M0_UT_ASSERT(tx.t_sm.sm_rc == 0);
	M0_UT_ASSERT(m0_be_tx_state(&tx) == M0_BTS_PREPARE);

	m0_be_tx_prep(&tx, &m0_be_tx_credit_invalid);
	M0_UT_ASSERT(tx.t_sm.sm_rc == 0);
	M0_UT_ASSERT(m0_be_tx_state(&tx) == M0_BTS_PREPARE);

	m0_be_tx_get(&tx);

	rc = m0_be_tx_open_sync(&tx);
	M0_UT_ASSERT(rc != 0);
	M0_UT_ASSERT(m0_be_tx_state(&tx) == M0_BTS_FAILED);

	m0_be_tx_put(&tx);

	m0_be_tx_fini(&tx);

	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);
}

void m0_be_ut_tx_empty(void)
{
	struct m0_be_ut_backend ut_be;
	struct m0_be_tx         tx;
	int                     rc;
	int			i;
	struct m0_be_tx_credit  credit[] = {
		M0_BE_TX_CREDIT(0, 0),
		M0_BE_TX_CREDIT(1, sizeof(void *)),
	};

	M0_SET0(&ut_be);
	m0_be_ut_backend_init(&ut_be);

	for (i = 0; i < ARRAY_SIZE(credit); ++i) {
		m0_be_ut_tx_init(&tx, &ut_be);

		m0_be_tx_prep(&tx, &credit[i]);

		rc = m0_be_tx_open_sync(&tx);
		M0_UT_ASSERT(rc == 0);

		m0_be_tx_close_sync(&tx);
		m0_be_tx_fini(&tx);
	}

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
		struct m0_uint128    u128;
		struct be_ut_complex complex;
	} captured;
};

enum { SHIFT = 0 };

static void
be_ut_transact(struct be_ut_tx_x *x, struct m0_be_seg *seg, void **alloc)
{
	int rc;

	m0_be_tx_prep(&x->tx, &x->cred);

	rc = m0_be_tx_open_sync(&x->tx);
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

	m0_be_tx_close(&x->tx);
}

/**
 * @param nr  Number of transactions to use.
 */
static void be_ut_tx_test(size_t nr)
{
	struct m0_be_ut_backend ut_be;
	struct m0_be_ut_seg     ut_seg;
	void                   *alloc;
	struct be_ut_tx_x      *x;
	struct be_ut_tx_x       xs[] = {
		{
			.size          = sizeof(struct m0_uint128),
			.captured.u128 = M0_UINT128(0xdeadd00d8badf00d,
						    0x5ca1ab1e7e1eca57)
		},
		{
			.size             = sizeof(struct be_ut_complex),
			.captured.complex = { .real = 18, .imag = 4 }
		},
		{ .size = 0 } /* terminator */
	};

	M0_PRE(0 < nr && nr < ARRAY_SIZE(xs));
	xs[nr].size = 0;

	M0_SET0(&ut_be);
	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, &ut_be, 1 << 20);
	be_ut_tx_alloc_init(&alloc, &ut_seg.bus_seg);

	for (x = xs; x->size != 0; ++x) {
		m0_be_ut_tx_init(&x->tx, &ut_be);
		m0_be_tx_get(&x->tx);
		x->cred = M0_BE_TX_CREDIT(1, x->size);
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

	m0_be_ut_seg_check_persistence(&ut_seg);

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

/** constants for backend UT for transaction persistence */
enum {
	BE_UT_TX_P_SEG_SIZE	= 0x4000,
	BE_UT_TX_P_TX_NR	= 0x400,
	BE_UT_TX_P_REG_NR	= 0x10,
	BE_UT_TX_P_REG_SIZE_MAX = 0x100,
};

static void be_ut_tx_reg_rand(struct m0_be_reg *reg, struct m0_be_seg *seg,
			      unsigned *seed)
{
	uintptr_t addr;

	reg->br_seg = seg;
	reg->br_size = rand_r(seed) % BE_UT_TX_P_REG_SIZE_MAX + 1;
	addr = rand_r(seed) % (seg->bs_size - reg->br_size - seg->bs_reserved);
	reg->br_addr = seg->bs_addr + seg->bs_reserved + addr;
}

static void be_ut_tx_reg_rand_fill(struct m0_be_reg *reg, unsigned *seed)
{
	int i;

	for (i = 0; i < reg->br_size; ++i)
		((char *) reg->br_addr)[i] = rand_r(seed) & 0xFF;
}

void m0_be_ut_tx_persistence(void)
{
	static struct m0_be_reg regs[BE_UT_TX_P_REG_NR];
	struct m0_be_ut_backend ut_be;
	struct m0_be_tx_credit  credit;
	struct m0_be_ut_seg     ut_seg;
	struct m0_be_seg       *seg = &ut_seg.bus_seg;
	struct m0_be_tx         tx;
	unsigned                seed = 0;
	int                     i;
	int                     j;
	int                     rc;

	M0_SET0(&ut_be);
	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, &ut_be, BE_UT_TX_P_SEG_SIZE);

	for (j = 0; j < BE_UT_TX_P_TX_NR; ++j) {
		M0_SET0(&tx);
		m0_be_ut_tx_init(&tx, &ut_be);

		for (i = 0; i < ARRAY_SIZE(regs); ++i)
			be_ut_tx_reg_rand(&regs[i], seg, &seed);

		M0_SET0(&credit);
		for (i = 0; i < ARRAY_SIZE(regs); ++i) {
			m0_be_tx_credit_add(&credit, &M0_BE_TX_CREDIT(
						    1, regs[i].br_size));
		}

		m0_be_tx_prep(&tx, &credit);

		rc = m0_be_tx_open_sync(&tx);
		M0_UT_ASSERT(rc == 0);

		for (i = 0; i < ARRAY_SIZE(regs); ++i) {
			be_ut_tx_reg_rand_fill(&regs[i], &seed);
			m0_be_tx_capture(&tx, &regs[i]);
		}

		m0_be_tx_get(&tx);
		m0_be_tx_close(&tx);
		rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_PLACED),
					M0_TIME_NEVER);
		M0_UT_ASSERT(rc == 0);

		m0_be_ut_seg_check_persistence(&ut_seg);

		m0_be_tx_put(&tx);
		rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_DONE),
					M0_TIME_NEVER);
		M0_UT_ASSERT(rc == 0);
		m0_be_tx_fini(&tx);
	}

	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);
}

enum {
	BE_UT_TX_F_SEG_SIZE  = 0x20000,
	BE_UT_TX_F_TX_NR     = 0x800,
	BE_UT_TX_F_TX_CONCUR = 0x100,
};

void m0_be_ut_tx_fast(void)
{
	struct m0_be_ut_backend ut_be;
	static struct m0_be_tx  txs[BE_UT_TX_F_TX_CONCUR];
	struct m0_be_ut_seg     ut_seg;
	struct m0_be_seg       *seg = &ut_seg.bus_seg;
	struct m0_be_reg        reg;
	int                     i;
	int                     rc;

	M0_SET0(&ut_be);
	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, &ut_be, BE_UT_TX_F_SEG_SIZE);

	reg = M0_BE_REG(seg, 1, seg->bs_addr + seg->bs_reserved);
	for (i = 0; i < BE_UT_TX_F_TX_NR + ARRAY_SIZE(txs); ++i) {
		struct m0_be_tx *tx = &txs[i % ARRAY_SIZE(txs)];

		if (i >= ARRAY_SIZE(txs)) {
			m0_be_tx_close_sync(tx);
			m0_be_tx_fini(tx);
		}

		if (i < BE_UT_TX_F_TX_NR) {
			m0_be_ut_tx_init(tx, &ut_be);
			m0_be_tx_prep(tx, &M0_BE_TX_CREDIT(1, reg.br_size));
			rc = m0_be_tx_open_sync(tx);
			M0_UT_ASSERT(rc == 0);
			m0_be_tx_capture(tx, &reg);
			++reg.br_addr;
		}
	}

	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);
}

enum {
	BE_UT_TX_C_THREAD_NR     = 0x10,
	BE_UT_TX_C_TX_PER_THREAD = 0x40,
};

struct be_ut_tx_thread_state {
	struct m0_thread	 tts_thread;
	struct m0_be_ut_backend *tts_ut_be;
	bool                     tts_exclusive;
};

static void be_ut_tx_run_tx_helper(struct be_ut_tx_thread_state *state,
				   struct m0_be_tx *tx,
				   bool exclusive)
{
	M0_SET0(tx);
	m0_be_ut_tx_init(tx, state->tts_ut_be);

	if (exclusive)
		m0_be_tx_exclusive_open_sync(tx);
	else
		m0_be_tx_open_sync(tx);

	m0_be_tx_close_sync(tx);
	m0_be_tx_fini(tx);
}

static void be_ut_tx_thread(struct be_ut_tx_thread_state *state)
{
	struct m0_be_tx tx;
	int		i;

	if (state->tts_exclusive)
		be_ut_tx_run_tx_helper(state, &tx, true);
	else
		for (i = 0; i < BE_UT_TX_C_TX_PER_THREAD; ++i)
			be_ut_tx_run_tx_helper(state, &tx, false);

	m0_be_ut_backend_thread_exit(state->tts_ut_be);
}

void m0_be_ut_tx_concurrent_helper(bool exclusive)
{
	static struct be_ut_tx_thread_state threads[BE_UT_TX_C_THREAD_NR];
	struct m0_be_ut_backend             ut_be;
	int                                 i;
	int                                 rc;

	M0_SET0(&ut_be);
	m0_be_ut_backend_init(&ut_be);

	for (i = 0; i < ARRAY_SIZE(threads); ++i) {
		threads[i].tts_ut_be     = &ut_be;
		threads[i].tts_exclusive = !exclusive ? false :
			(i == ARRAY_SIZE(threads) / 2 ||
			 i == ARRAY_SIZE(threads) / 4 ||
			 i == ARRAY_SIZE(threads) * 3 / 4);
		rc = M0_THREAD_INIT(&threads[i].tts_thread,
				    struct be_ut_tx_thread_state *, NULL,
				    &be_ut_tx_thread, &threads[i],
				    "#%dbe_ut_tx", i);
		M0_UT_ASSERT(rc == 0);
	}
	for (i = 0; i < ARRAY_SIZE(threads); ++i) {
		rc = m0_thread_join(&threads[i].tts_thread);
		M0_UT_ASSERT(rc == 0);
		m0_thread_fini(&threads[i].tts_thread);
	}

	m0_be_ut_backend_fini(&ut_be);
}

void m0_be_ut_tx_concurrent(void)
{
	m0_be_ut_tx_concurrent_helper(false);
}

void m0_be_ut_tx_concurrent_excl(void)
{
	m0_be_ut_tx_concurrent_helper(true);
}

enum {
	BE_UT_TX_CAPTURING_SEG_SIZE = 0x10000,
	BE_UT_TX_CAPTURING_TX_NR    = 0x10,
	BE_UT_TX_CAPTURING_NR	    = 0x100,
	BE_UT_TX_CAPTURING_RANGE    = 0x20,
};

M0_BASSERT(BE_UT_TX_CAPTURING_RANGE >= sizeof(uint64_t));

void m0_be_ut_tx_capturing(void)
{
	struct m0_be_ut_backend  ut_be;
	struct m0_be_tx_credit	 cred = M0_BE_TX_CREDIT_TYPE(uint64_t);
	struct m0_be_ut_txc	 tc = {};
	struct m0_be_ut_seg	 ut_seg;
	struct m0_be_seg	*seg = &ut_seg.bus_seg;
	struct m0_be_tx		 tx;
	unsigned int		 seed = 0;
	uint64_t		*ptr;
	int			 i;
	int			 j;
	int			 rc;

	M0_SET0(&ut_be);
	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, &ut_be, BE_UT_TX_CAPTURING_SEG_SIZE);
	m0_be_ut_txc_init(&tc);

	m0_be_tx_credit_mul(&cred, BE_UT_TX_CAPTURING_NR);
	for (i = 0; i < BE_UT_TX_CAPTURING_TX_NR; ++i) {
		m0_be_ut_tx_init(&tx, &ut_be);
		m0_be_tx_prep(&tx, &cred);
		rc = m0_be_tx_open_sync(&tx);
		M0_UT_ASSERT(rc == 0);

		m0_be_ut_txc_start(&tc, &tx, seg);
		for (j = 0; j < BE_UT_TX_CAPTURING_NR; ++j) {
			ptr = seg->bs_addr + m0_be_seg_reserved(seg) +
			      rand_r(&seed) %
			      (BE_UT_TX_CAPTURING_RANGE - sizeof(*ptr));
			*ptr = rand_r(&seed);
			m0_be_tx_capture(&tx, &M0_BE_REG_PTR(seg, ptr));

			m0_be_ut_txc_check(&tc, &tx);
		}

		m0_be_tx_close_sync(&tx);
		m0_be_tx_fini(&tx);
	}

	m0_be_ut_txc_fini(&tc);
	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);
}

#undef M0_TRACE_SUBSYSTEM
