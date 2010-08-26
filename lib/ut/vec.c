/* -*- C -*- */

#include "lib/ut.h"
#include "lib/cdefs.h"     /* ARRAY_SIZE */
#include "lib/vec.h"
#include "lib/assert.h"

enum {
	NR = 255,
	IT = 6
};

static c2_bcount_t segs[NR * IT];

static struct c2_vec t = {
	.v_nr    = NR * IT,
	.v_count = segs
};

void test_vec(void)
{
	int         i;
	int         it;
	c2_bcount_t count;
	c2_bcount_t sum0;
	c2_bcount_t sum1;
	c2_bcount_t step;
	bool        eov;

	struct c2_vec_cursor c;

	for (count = 0, it = 1, sum0 = i = 0; i < ARRAY_SIZE(segs); ++i) {
		segs[i] = count * it;
		sum0 += segs[i];
		if (++count == NR) {
			count = 0;
			++it;
		}
	};

	C2_UT_ASSERT(c2_vec_count(&t) == sum0);

	c2_vec_cursor_init(&c, &t);
	for (i = 0; i < sum0; ++i) {
		eov = c2_vec_cursor_move(&c, 1);
		C2_UT_ASSERT(eov == (i == sum0 - 1));
	}

	c2_vec_cursor_init(&c, &t);
	count = 0; 
	it = 1;
	sum1 = 0;
	while (!c2_vec_cursor_move(&c, 0)) {
		if (count * it != 0) {
			step = c2_vec_cursor_step(&c);
			sum1 += step;
			C2_UT_ASSERT(step == count * it);
			eov = c2_vec_cursor_move(&c, step);
			C2_UT_ASSERT(eov == (sum1 == sum0));
		}
		if (++count == NR) {
			count = 0;
			++it;
		}
	}
	c2_vec_cursor_init(&c, &t);
	c2_vec_cursor_move(&c, sum0);
	C2_UT_ASSERT(c2_vec_cursor_move(&c, 0));
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
