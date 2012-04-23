/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 05/07/2010
 */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/ub.h"
#include "lib/ut.h"
#include "lib/vec.h"    /* C2_SEG_SIZE & C2_SEG_SHIFT */
#include "lib/memory.h"

struct test1 {
	int a;
};

void test_memory(void)
{
	void         *ptr1;
	struct test1 *ptr2;
	size_t        allocated;
	int           i;
	allocated = c2_allocated();
	ptr1 = c2_alloc(100);
	C2_UT_ASSERT(ptr1 != NULL);

	C2_ALLOC_PTR(ptr2);
	C2_UT_ASSERT(ptr2 != NULL);

	c2_free(ptr1);
	c2_free(ptr2);
	C2_UT_ASSERT(allocated == c2_allocated());

	/* Checking c2_alloc_aligned for buffer sizes from 4K to 64Kb. */
	for (i = 0; i <= C2_SEG_SIZE * 16; i += C2_SEG_SIZE / 2) {
		ptr1 = c2_alloc_aligned(i, C2_SEG_SHIFT);
		C2_UT_ASSERT(c2_addr_is_aligned(ptr1, C2_SEG_SHIFT));
		c2_free_aligned(ptr1, (size_t)i, C2_SEG_SHIFT);
	}

}

enum {
	UB_ITER   = 5000000,
	UB_SMALL  = 1,
	UB_MEDIUM = 17,
	UB_LARGE  = 512,
	UB_HUGE   = 128*1024
};

static void *ubx[UB_ITER];

static void ub_init(void)
{
	C2_SET_ARR0(ubx);
}

static void ub_free(int i)
{
	c2_free(ubx[i]);
}

static void ub_small(int i)
{
	ubx[i] = c2_alloc(UB_SMALL);
}

static void ub_medium(int i)
{
	ubx[i] = c2_alloc(UB_MEDIUM);
}

static void ub_large(int i)
{
	ubx[i] = c2_alloc(UB_LARGE);
}

static void ub_huge(int i)
{
	ubx[i] = c2_alloc(UB_HUGE);
}

#if 0
static void ub_free_all(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ubx); ++i)
		c2_free(ubx[i]);
}
#endif

struct c2_ub_set c2_memory_ub = {
	.us_name = "memory-ub",
	.us_init = ub_init,
	.us_fini = NULL,
	.us_run  = {
		{ .ut_name  = "alloc-small",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_small },

		{ .ut_name  = "free-small",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_free },

		{ .ut_name  = "alloc-medium",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_medium },

		{ .ut_name  = "free-medium",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_free },

		{ .ut_name  = "alloc-large",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_large },

		{ .ut_name  = "free-large",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_free },

		{ .ut_name  = "alloc-huge",
		  .ut_iter  = UB_ITER/1000,
		  .ut_round = ub_huge },

		{ .ut_name  = "free-huge",
		  .ut_iter  = UB_ITER/1000,
		  .ut_round = ub_free },

		{ .ut_name = NULL }
	}
};


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
