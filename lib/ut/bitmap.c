/* -*- C -*- */

#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/bitmap.h"
#include "lib/assert.h"

enum {
	UT_BITMAP_SIZE = 120
};

void test_bitmap(void)
{
	struct c2_bitmap bm;
	size_t idx;

	/* verify the bounds of size macro */
	C2_UT_ASSERT(C2_BITMAP_WORDS(0) == 0);
	C2_UT_ASSERT(C2_BITMAP_WORDS(1) == 1);
	C2_UT_ASSERT(C2_BITMAP_WORDS(63) == 1);
	C2_UT_ASSERT(C2_BITMAP_WORDS(64) == 1);
	C2_UT_ASSERT(C2_BITMAP_WORDS(65) == 2);
	C2_UT_ASSERT(C2_BITMAP_WORDS(UT_BITMAP_SIZE) == 2);

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
		if (idx != 1) {
			C2_UT_ASSERT(c2_bitmap_get(&bm, idx) == false);
		} else {
			C2_UT_ASSERT(c2_bitmap_get(&bm, idx) == true);
		}
	}

	c2_bitmap_set(&bm, 2, true);
	for (idx = 0; idx < UT_BITMAP_SIZE; ++idx) {
		if (idx != 1 && idx != 2) {
			C2_UT_ASSERT(c2_bitmap_get(&bm, idx) == false);
		} else {
			C2_UT_ASSERT(c2_bitmap_get(&bm, idx) == true);
		}
	}

	c2_bitmap_set(&bm, 64, true);
	for (idx = 0; idx < UT_BITMAP_SIZE; ++idx) {
		if (idx != 1 && idx != 2 && idx != 64) {
			C2_UT_ASSERT(c2_bitmap_get(&bm, idx) == false);
		} else {
			C2_UT_ASSERT(c2_bitmap_get(&bm, idx) == true);
		}
	}

	c2_bitmap_set(&bm, 2, false);
	for (idx = 0; idx < UT_BITMAP_SIZE; ++idx) {
		if (idx != 1 && idx != 64) {
			C2_UT_ASSERT(c2_bitmap_get(&bm, idx) == false);
		} else {
			C2_UT_ASSERT(c2_bitmap_get(&bm, idx) == true);
		}
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
