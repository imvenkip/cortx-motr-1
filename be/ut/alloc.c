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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 3-Jun-2013
 */

#include "be/alloc.h"

#include "lib/memory.h"		/* m0_addr_is_aligned */
#include "lib/misc.h"		/* M0_SET_ARR0 */
#include "lib/thread.h"		/* m0_thread */
#include "ut/ut.h"		/* M0_UT_ASSERT */
#include "be/ut/helper.h"	/* m0_be_ut_backend */

#include <stdlib.h>		/* rand_r */
#include <string.h>		/* memset */

enum {
	BE_UT_ALLOC_SEG_SIZE = 0x400000,
	BE_UT_ALLOC_SIZE     = 0x100,
	BE_UT_ALLOC_SHIFT    = 13,
	BE_UT_ALLOC_PTR_NR   = 0x100,
	BE_UT_ALLOC_NR	     = 0x1000,
	BE_UT_ALLOC_MT_NR    = 0x100,
	BE_UT_ALLOC_THR_NR   = 0x10,
	BE_UT_ALLOC_TX_NR    = 0x400,
};

struct be_ut_alloc_thread_state {
	struct m0_thread ats_thread;
	/** pointers array for this thread */
	void		*ats_ptr[BE_UT_ALLOC_PTR_NR];
	/** number of interations for this thread */
	int		 ats_nr;
};

static struct m0_be_ut_backend	       be_ut_alloc_backend;
static struct m0_be_ut_seg	       be_ut_alloc_seg;
static struct be_ut_alloc_thread_state be_ut_ts[BE_UT_ALLOC_THR_NR];

M0_INTERNAL void m0_be_ut_alloc_init_fini(void)
{
	struct m0_be_ut_seg    ut_seg;
	struct m0_be_allocator a;
	int		       rc;

	m0_be_ut_seg_init(&ut_seg, NULL, BE_UT_ALLOC_SEG_SIZE);
	rc = m0_be_allocator_init(&a, &ut_seg.bus_seg);
	M0_UT_ASSERT(rc == 0);
	m0_be_allocator_fini(&a);
	m0_be_ut_seg_fini(&ut_seg);
}

M0_INTERNAL void m0_be_ut_alloc_create_destroy(void)
{
	struct m0_be_ut_seg ut_seg;

	m0_be_ut_backend_init(&be_ut_alloc_backend);
	m0_be_ut_seg_init(&ut_seg, NULL, BE_UT_ALLOC_SEG_SIZE);
	m0_be_ut_seg_check_persistence(&ut_seg);

	m0_be_ut_seg_allocator_init(&ut_seg, &be_ut_alloc_backend);
	m0_be_ut_seg_check_persistence(&ut_seg);

	m0_be_ut_seg_allocator_fini(&ut_seg, &be_ut_alloc_backend);
	m0_be_ut_seg_check_persistence(&ut_seg);

	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&be_ut_alloc_backend);
}

static void be_ut_alloc_ptr_handle(struct m0_be_allocator *a,
				   struct m0_be_ut_backend *ut_be,
				   void **p,
				   unsigned *seed)
{
	struct m0_be_tx_credit	credit = {};
	enum m0_be_allocator_op optype;
	struct m0_be_ut_txc	tc = {};
	struct m0_be_op		op;
	struct m0_be_tx		tx_;
	struct m0_be_tx	       *tx = ut_be == NULL ? NULL : &tx_;
	m0_bcount_t		size;
	unsigned		shift;
	int			rc;

	size = rand_r(seed) % BE_UT_ALLOC_SIZE + 1;
	shift = rand_r(seed) % BE_UT_ALLOC_SHIFT;
	optype = *p == NULL ? M0_BAO_ALLOC_ALIGNED : M0_BAO_FREE_ALIGNED;

	if (ut_be != NULL) {
		m0_be_ut_tx_init(tx, ut_be);

		m0_be_allocator_credit(a, optype, size, shift, &credit);
		m0_be_tx_prep(tx, &credit);

		rc = m0_be_tx_open_sync(tx);
		M0_UT_ASSERT(rc == 0);
		m0_be_ut_txc_init(&tc);
		m0_be_ut_txc_start(&tc, tx, a->ba_seg);
	}

	m0_be_op_init(&op);
	if (*p == NULL) {
		m0_be_alloc_aligned(a, tx, &op, p, size, shift);
		rc = m0_be_op_wait(&op);
		M0_UT_ASSERT(*p != NULL);
		M0_UT_ASSERT(m0_addr_is_aligned(*p, shift));
	} else {
		m0_be_free_aligned(a, tx, &op, *p);
		rc = m0_be_op_wait(&op);
		*p = NULL;
	}
	M0_UT_ASSERT(rc == 0);
	m0_be_op_fini(&op);

	if (ut_be != NULL) {
		m0_be_ut_txc_check(&tc, tx);
		m0_be_ut_txc_fini(&tc);
		m0_be_tx_close_sync(tx);
		m0_be_tx_fini(tx);
	}
}

static void be_ut_alloc_thread(int index)
{
	struct be_ut_alloc_thread_state *ts = &be_ut_ts[index];
	struct m0_be_allocator		*a;
	unsigned			 seed = index;
	int				 i;
	int				 j;

	a = m0_be_seg_allocator(&be_ut_alloc_seg.bus_seg);
	M0_SET_ARR0(ts->ats_ptr);
	for (j = 0; j < ts->ats_nr; ++j) {
		i = rand_r(&seed) % ARRAY_SIZE(ts->ats_ptr);
		be_ut_alloc_ptr_handle(a, NULL, &ts->ats_ptr[i], &seed);
	}
	for (i = 0; i < BE_UT_ALLOC_PTR_NR; ++i) {
		if (ts->ats_ptr[i] != NULL)
			be_ut_alloc_ptr_handle(a, NULL, &ts->ats_ptr[i], &seed);
	}
}

static void be_ut_alloc_mt(int nr)
{
	int rc;
	int i;

	M0_SET_ARR0(be_ut_ts);
	for (i = 0; i < nr; ++i) {
		be_ut_ts[i].ats_nr = nr == 1 ? BE_UT_ALLOC_NR :
					       BE_UT_ALLOC_MT_NR;
	}
	m0_be_ut_seg_init(&be_ut_alloc_seg, NULL, BE_UT_ALLOC_SEG_SIZE);
	m0_be_ut_seg_allocator_init(&be_ut_alloc_seg, NULL);
	for (i = 0; i < nr; ++i) {
		rc = M0_THREAD_INIT(&be_ut_ts[i].ats_thread, int, NULL,
				    &be_ut_alloc_thread, i,
				    "#%dbe_ut_alloc", i);
		M0_UT_ASSERT(rc == 0);
	}
	for (i = 0; i < nr; ++i) {
		m0_thread_join(&be_ut_ts[i].ats_thread);
		m0_thread_fini(&be_ut_ts[i].ats_thread);
	}
	m0_be_ut_seg_allocator_fini(&be_ut_alloc_seg, NULL);
	m0_be_ut_seg_fini(&be_ut_alloc_seg);
}

M0_INTERNAL void m0_be_ut_alloc_multiple(void)
{
	be_ut_alloc_mt(1);
}

M0_INTERNAL void m0_be_ut_alloc_concurrent(void)
{
	be_ut_alloc_mt(BE_UT_ALLOC_THR_NR);
}

M0_INTERNAL void m0_be_ut_alloc_transactional(void)
{
	struct m0_be_ut_backend *ut_be = &be_ut_alloc_backend;
	struct m0_be_ut_seg	 ut_seg;
	struct m0_be_allocator	*a = m0_be_seg_allocator(&ut_seg.bus_seg);
	void			*ptrs[BE_UT_ALLOC_PTR_NR];
	unsigned		 seed = 0;
	int			 i;
	int			 j;

	M0_SET0(ut_be);
	m0_be_ut_backend_init(ut_be);
	m0_be_ut_seg_init(&ut_seg, ut_be, BE_UT_ALLOC_SEG_SIZE);
	m0_be_ut_seg_check_persistence(&ut_seg);

	m0_be_ut_seg_allocator_init(&ut_seg, ut_be);
	m0_be_ut_seg_check_persistence(&ut_seg);

	M0_SET_ARR0(ptrs);
	for (j = 0; j < BE_UT_ALLOC_TX_NR; ++j) {
		i = rand_r(&seed) % ARRAY_SIZE(ptrs);
		be_ut_alloc_ptr_handle(a, ut_be, &ptrs[i], &seed);
		m0_be_ut_seg_check_persistence(&ut_seg);
	}
	for (i = 0; i < ARRAY_SIZE(ptrs); ++i) {
		if (ptrs[i] != NULL)
			be_ut_alloc_ptr_handle(a, ut_be, &ptrs[i], &seed);
		m0_be_ut_seg_check_persistence(&ut_seg);
	}

	m0_be_ut_seg_allocator_fini(&ut_seg, ut_be);
	m0_be_ut_seg_check_persistence(&ut_seg);

	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(ut_be);
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
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
