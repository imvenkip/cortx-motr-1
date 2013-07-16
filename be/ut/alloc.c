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
#include "be/ut/helper.h"	/* m0_be_ut_h */

#include <stdlib.h>		/* rand_r */
#include <string.h>		/* memset */

enum {
	BE_UT_ALLOC_SIZE   = 0x100,
	BE_UT_ALLOC_SHIFT  = 13,
	BE_UT_ALLOC_PTR_NR = 0x100,
	BE_UT_ALLOC_NR	   = 0x1000,
	BE_UT_ALLOC_MT_NR  = 0x100,
	BE_UT_ALLOC_THR_NR = 0x10,
};

struct be_ut_alloc_thread_state {
	struct m0_thread ats_thread;
	/** pointers array for this thread */
	void		*ats_ptr[BE_UT_ALLOC_PTR_NR];
	/** number of interations for this thread */
	int		 ats_nr;
};

static struct m0_be_ut_h	       be_ut_alloc_h;
static struct be_ut_alloc_thread_state be_ut_ts[BE_UT_ALLOC_THR_NR];

M0_INTERNAL void m0_be_ut_alloc_init_fini(void)
{
	struct m0_be_allocator a;
	int		       rc;

	m0_be_ut_seg_create_open(&be_ut_alloc_h);
	rc = m0_be_allocator_init(&a, &be_ut_alloc_h.buh_seg);
	M0_UT_ASSERT(rc == 0);
	m0_be_allocator_fini(&a);
	m0_be_ut_seg_close_destroy(&be_ut_alloc_h);
}

M0_INTERNAL void m0_be_ut_alloc_create_destroy(void)
{
	m0_be_ut_h_init(&be_ut_alloc_h);
	m0_be_ut_h_fini(&be_ut_alloc_h);
}

static void be_ut_alloc_thread(int index)
{
	struct be_ut_alloc_thread_state *ts = &be_ut_ts[index];
	struct m0_be_op			 op;
	unsigned int			 seed = index;
	m0_bcount_t			 size;
	unsigned			 shift;
	int				 i;
	int				 j;
	void				*p;

	memset(&ts->ats_ptr, 0, sizeof(ts->ats_ptr));
	for (j = 0; j < ts->ats_nr; ++j) {
		i = rand_r(&seed) % BE_UT_ALLOC_PTR_NR;
		p = ts->ats_ptr[i];
		m0_be_op_init(&op);
		if (p == NULL) {
			size = (rand_r(&seed) % BE_UT_ALLOC_SIZE) + 1;
			shift = rand_r(&seed) % BE_UT_ALLOC_SHIFT;
			p = m0_be_alloc(be_ut_alloc_h.buh_allocator, NULL, &op,
					/* XXX */ size, shift);
			M0_UT_ASSERT(p != NULL);
			M0_UT_ASSERT(m0_addr_is_aligned(p, shift));
			if (p != NULL)
				memset(p, 0xFF, size);
		} else {
			m0_be_free(be_ut_alloc_h.buh_allocator, NULL, &op,
				   /*XXX*/ p);
			p = NULL;
		}
		m0_be_op_fini(&op);
		ts->ats_ptr[i] = p;
	}
	for (i = 0; i < BE_UT_ALLOC_PTR_NR; ++i) {
		m0_be_op_init(&op);
		m0_be_free(be_ut_alloc_h.buh_allocator, NULL, &op,
			   /*XXX*/ ts->ats_ptr[i]);
		m0_be_op_fini(&op);
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
	m0_be_ut_h_init(&be_ut_alloc_h);
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
	m0_be_ut_h_fini(&be_ut_alloc_h);
}

M0_INTERNAL void m0_be_ut_alloc_multiple(void)
{
	be_ut_alloc_mt(1);
}

M0_INTERNAL void m0_be_ut_alloc_concurrent(void)
{
	be_ut_alloc_mt(BE_UT_ALLOC_THR_NR);
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
