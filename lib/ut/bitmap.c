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
 * Original author: Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 02/28/2011
 */

#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/bitmap.h"
#include "lib/assert.h"

enum {
	UT_BITMAP_SIZE = 120
};

static void test_bitmap_copy(void)
{
	struct c2_bitmap src;
	struct c2_bitmap dst;
	size_t dst_nr;
	size_t i;
	int n;

	C2_UT_ASSERT(c2_bitmap_init(&src, UT_BITMAP_SIZE) == 0);
	for (i = 0; i < UT_BITMAP_SIZE; i += 3)
		c2_bitmap_set(&src, i, true);

	for (n = 1; n < 3; ++n) {
		/* n == 1: equal sized, n == 2: dst size is bigger */
		dst_nr = n * UT_BITMAP_SIZE;
		C2_UT_ASSERT(c2_bitmap_init(&dst, dst_nr) == 0);
		for (i = 1; i < dst_nr; i += 2)
			c2_bitmap_set(&dst, i, true);

		c2_bitmap_copy(&dst, &src);
		for (i = 0; i < UT_BITMAP_SIZE; ++i)
			C2_UT_ASSERT(c2_bitmap_get(&src, i) ==
				     c2_bitmap_get(&dst, i));
		for (; i < dst_nr; ++i)
			C2_UT_ASSERT(!c2_bitmap_get(&dst, i));
		c2_bitmap_fini(&dst);
	}
	c2_bitmap_fini(&src);
}

void test_bitmap(void)
{
	struct c2_bitmap bm;
	size_t idx;

	C2_UT_ASSERT(c2_bitmap_init(&bm, UT_BITMAP_SIZE) == 0);
	C2_UT_ASSERT(bm.b_nr == UT_BITMAP_SIZE);
	C2_UT_ASSERT(bm.b_words != NULL);

	for (idx = 0; idx < UT_BITMAP_SIZE; ++idx) {
		C2_UT_ASSERT(c2_bitmap_get(&bm, idx) == false);
	}

	c2_bitmap_set(&bm, 0, true);
	C2_UT_ASSERT(c2_bitmap_get(&bm, 0) == true);
	c2_bitmap_set(&bm, 0, false);

	c2_bitmap_set(&bm, 1, true);
	for (idx = 0; idx < UT_BITMAP_SIZE; ++idx) {
		C2_UT_ASSERT(c2_bitmap_get(&bm, idx) == (idx == 1));
	}

	c2_bitmap_set(&bm, 2, true);
	for (idx = 0; idx < UT_BITMAP_SIZE; ++idx) {
		C2_UT_ASSERT(c2_bitmap_get(&bm, idx) == (idx == 1 || idx == 2));
	}

	c2_bitmap_set(&bm, 64, true);
	for (idx = 0; idx < UT_BITMAP_SIZE; ++idx) {
		C2_UT_ASSERT(c2_bitmap_get(&bm, idx) ==
			     (idx == 1 || idx == 2 || idx == 64));
	}

	c2_bitmap_set(&bm, 2, false);
	for (idx = 0; idx < UT_BITMAP_SIZE; ++idx) {
		C2_UT_ASSERT(c2_bitmap_get(&bm, idx) ==
			     (idx == 1 || idx == 64));
	}

	c2_bitmap_set(&bm, 1, false);
	c2_bitmap_set(&bm, 64, false);
	for (idx = 0; idx < UT_BITMAP_SIZE; ++idx) {
		C2_UT_ASSERT(c2_bitmap_get(&bm, idx) == false);
	}

	c2_bitmap_fini(&bm);
	C2_UT_ASSERT(bm.b_nr == 0);
	C2_UT_ASSERT(bm.b_words == NULL);

	C2_UT_ASSERT(c2_bitmap_init(&bm, 0) == 0);
	C2_UT_ASSERT(bm.b_nr == 0);
	C2_UT_ASSERT(bm.b_words != NULL);
	c2_bitmap_fini(&bm);

	test_bitmap_copy();
}

enum {
	UB_ITER = 100000
};

static struct c2_bitmap ub_bm;

static void ub_init(void)
{
	c2_bitmap_init(&ub_bm, UT_BITMAP_SIZE);
}

static void ub_fini(void)
{
	c2_bitmap_fini(&ub_bm);
}

static void ub_set0(int i)
{
	size_t idx;

	for (idx = 0; idx < UT_BITMAP_SIZE; ++idx)
		c2_bitmap_set(&ub_bm, idx, false);
}

static void ub_set1(int i)
{
	size_t idx;

	for (idx = 0; idx < UT_BITMAP_SIZE; ++idx)
		c2_bitmap_set(&ub_bm, idx, true);
}

static void ub_get(int i)
{
	size_t idx;

	for (idx = 0; idx < UT_BITMAP_SIZE; ++idx)
		c2_bitmap_get(&ub_bm, idx);
}

struct c2_ub_set c2_bitmap_ub = {
	.us_name = "bitmap-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ut_name = "set0",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_set0 },
		{ .ut_name = "set1",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_set1 },
		{ .ut_name = "get",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_get },
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
