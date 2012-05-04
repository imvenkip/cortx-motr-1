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
 * Original creation date: 09/09/2010
 */

#include "lib/ub.h"
#include "lib/ut.h"
#include "lib/types.h"                /* uint64_t */
#include "lib/tlist.h"

struct foo {
	uint64_t        f_payload;
	uint64_t        f_magic;
	struct c2_tlink f_linkage0;
	struct c2_tlink f_linkage1;
	struct c2_tlink f_linkage2;
};

enum {
	N  = 6,
	NR = 128
};

enum {
	fl0_magix = 0xab5ce55edba1b0a0
};

static const struct c2_tl_descr fl0 = C2_TL_DESCR("foo-s of bar",
						  struct foo,
						  f_linkage0,
						  f_magic,
						  fl0_magix,
						  0xba1dba11adba0bab);

static const struct c2_tl_descr fl1 = C2_TL_DESCR("other foo-s of bar",
						  struct foo,
						  f_linkage1,
						  f_magic,
						  0xab5ce55edba1b0a0,
						  0xbabe1b0ccacc1078);

static const struct c2_tl_descr fl2 = C2_TL_DESCR("unchecked foo-s of bar",
						  struct foo,
						  f_linkage2,
						  f_magic,
						  0,
						  0x0123456789abcdef);

static struct c2_tl  head0[N];
static struct c2_tl  head1[N];
static struct c2_tl  head2[N];
static struct foo    amb[NR];

struct bar {
	uint64_t        b_payload;
	struct c2_tlink b_linkage;
	uint64_t        b_magic;
};

C2_TL_DESCR_DEFINE(bar, "bar-s", static, struct bar, b_linkage,
		   b_magic, 0xbeda551edcaca0ff, 0x7777777777777777);
C2_TL_DEFINE(bar, static, struct bar);

static struct c2_tl bhead;
static struct bar   B[NR];

void test_tlist(void)
{
	int           i;
	struct foo   *obj;
	struct c2_tl *head;
	uint64_t      sum;
	uint64_t      sumB;
	uint64_t      sum1;
	bool          done;
	struct bar   *b;

	C2_CASSERT(ARRAY_SIZE(head0) == ARRAY_SIZE(head1));
	/* link magic must be the same as in fl0, because the same ambient
	   object is shared. */
	C2_ASSERT(fl0.td_link_magic == fl1.td_link_magic);

	/* initialise */

	sum = 0;
	for (i = 0; i < ARRAY_SIZE(head0); ++i) {
		c2_tlist_init(&fl0, &head0[i]);
		c2_tlist_init(&fl1, &head1[i]);
		c2_tlist_init(&fl2, &head2[i]);
	}
	for (i = 0, obj = amb; i < ARRAY_SIZE(amb); ++i, ++obj) {
		c2_tlink_init(&fl0, obj);
		c2_tlink_init(&fl1, obj);
		c2_tlink_init(&fl2, obj);
		obj->f_payload = i;
		sum += i;
	}

	for (i = 0; i < ARRAY_SIZE(head0); ++i) {
		C2_UT_ASSERT(c2_tlist_is_empty(&fl0, &head0[i]));
		C2_UT_ASSERT(c2_tlist_length(&fl0, &head0[i]) == 0);

		C2_UT_ASSERT(c2_tlist_is_empty(&fl1, &head1[i]));
		C2_UT_ASSERT(c2_tlist_length(&fl1, &head1[i]) == 0);

		C2_UT_ASSERT(c2_tlist_is_empty(&fl2, &head2[i]));
		C2_UT_ASSERT(c2_tlist_length(&fl2, &head2[i]) == 0);
	}

	/* insert foo-s in the lists */

	for (i = 0, obj = amb; i < ARRAY_SIZE(amb); ++i, ++obj) {
		C2_UT_ASSERT(!c2_tlink_is_in(&fl0, obj));
		C2_UT_ASSERT(!c2_tlink_is_in(&fl1, obj));
		C2_UT_ASSERT(!c2_tlink_is_in(&fl2, obj));
		c2_tlist_add(&fl0, &head0[0], obj);
		C2_UT_ASSERT( c2_tlink_is_in(&fl0, obj));
		C2_UT_ASSERT(!c2_tlink_is_in(&fl1, obj));
		C2_UT_ASSERT(!c2_tlink_is_in(&fl2, obj));
		c2_tlist_add_tail(&fl1, &head1[0], obj);
		C2_UT_ASSERT(c2_tlink_is_in(&fl0, obj));
		C2_UT_ASSERT(c2_tlink_is_in(&fl1, obj));
		C2_UT_ASSERT(!c2_tlink_is_in(&fl2, obj));
		c2_tlist_add_tail(&fl2, &head2[0], obj);
		C2_UT_ASSERT(c2_tlink_is_in(&fl0, obj));
		C2_UT_ASSERT(c2_tlink_is_in(&fl1, obj));
		C2_UT_ASSERT(c2_tlink_is_in(&fl2, obj));

		C2_UT_ASSERT(c2_tlist_contains(&fl0, &head0[0], obj));
		C2_UT_ASSERT(c2_tlist_contains(&fl1, &head1[0], obj));
	}
	C2_UT_ASSERT(c2_tlist_length(&fl0, &head0[0]) == NR);
	C2_UT_ASSERT(c2_tlist_length(&fl1, &head1[0]) == NR);
	C2_UT_ASSERT(c2_tlist_length(&fl2, &head2[0]) == NR);

	C2_UT_ASSERT(c2_tlist_forall(&fl0, o, &head0[0],
				     ((struct foo *)o)->f_magic == fl0_magix));

	/* initialise bar-s */

	bar_tlist_init(&bhead);
	sumB = 0;
	for (i = 0, b = B; i < ARRAY_SIZE(B); ++i, ++b) {
		bar_tlink_init(b);
		C2_UT_ASSERT(!bar_tlink_is_in(b));
		bar_tlist_add(&bhead, b);
		C2_UT_ASSERT(bar_tlink_is_in(b));
		C2_UT_ASSERT(bar_tlist_contains(&bhead, b));
		sumB += (b->b_payload = 3*i);
	}

	C2_UT_ASSERT(bar_tlist_length(&bhead) == ARRAY_SIZE(B));
	C2_UT_ASSERT(c2_tl_forall(bar, bb, &bhead, bb->b_payload % 3 == 0));

	/* check that everything is in the lists */

	sum1 = 0;
	c2_tlist_for(&fl0, &head0[0], obj) {
		sum1 += obj->f_payload;
	} c2_tlist_endfor;

	C2_UT_ASSERT(sum == sum1);

	sum1 = 0;
	c2_tlist_for(&fl1, &head1[0], obj)
		sum1 += obj->f_payload;
	c2_tlist_endfor;
	C2_UT_ASSERT(sum == sum1);

	/* bulldozer the foo-s to the last head */

	for (head = head0, i = 0; i < N - 1; ++i, ++head) {
		int j;

		C2_UT_ASSERT(!c2_tlist_is_empty(&fl0, head));
		for (j = 0; (obj = c2_tlist_head(&fl0, head)) != NULL; ++j)
			c2_tlist_move_tail(&fl0, head + 1 + j % (N-i-1), obj);
		C2_UT_ASSERT(c2_tlist_is_empty(&fl0, head));
	}

	/* check that everything is still here */

	C2_UT_ASSERT(c2_tlist_length(&fl0, head) == NR);
	sum1 = 0;
	c2_tlist_for(&fl0, head, obj)
		sum1 += obj->f_payload;
	c2_tlist_endfor;
	C2_UT_ASSERT(sum == sum1);

	/* check that c2_tlist_for() works fine when the list is mutated */

	c2_tlist_for(&fl1, &head1[0], obj) {
		if (obj->f_payload % 2 == 0)
			c2_tlist_move(&fl1, &head1[1], obj);
	} c2_tlist_endfor;

	c2_tlist_for(&fl1, &head1[0], obj) {
		if (obj->f_payload % 2 != 0)
			c2_tlist_move(&fl1, &head1[1], obj);
	} c2_tlist_endfor;

	C2_UT_ASSERT(c2_tlist_length(&fl1, &head1[0]) == 0);
	C2_UT_ASSERT(c2_tlist_length(&fl1, &head1[1]) == NR);

	head = &head1[1];

	/* bubble sort the list */

	do {
		struct foo *prev;

		done = true;

		c2_tlist_for(&fl1, head, obj) {
			prev = c2_tlist_prev(&fl1, head, obj);
			if (prev != NULL && prev->f_payload > obj->f_payload) {
				c2_tlist_del(&fl1, obj);
				c2_tlist_add_before(&fl1, prev, obj);
				done = false;
			}
		} c2_tlist_endfor;
	} while (!done);

	/* check that the list is sorted */

	c2_tlist_for(&fl1, head, obj) {
		struct foo *nxt = c2_tlist_next(&fl1, head, obj);
		C2_UT_ASSERT(ergo(nxt != NULL,
				  obj->f_payload <= nxt->f_payload));
	} c2_tlist_endfor;

	/* check that magic-less iteration works. */

	sum1 = 0;
	c2_tlist_for(&fl2, &head2[0], obj)
		sum1 += obj->f_payload;
	c2_tlist_endfor;
	C2_UT_ASSERT(sum == sum1);

	/* reverse bar-s */

	c2_tl_for(bar, &bhead, b) {
		bar_tlist_move(&bhead, b);
	} c2_tl_endfor;

	/* check that bar list is reversed */

	for (i = 0, b = bar_tlist_head(&bhead); b != NULL;
	     b = bar_tlist_next(&bhead, b), ++i) {
		C2_UT_ASSERT(b->b_payload == 3*i);
	}

	/* finalise */

	for (i = 0, obj = amb; i < ARRAY_SIZE(amb); ++i, ++obj) {
		c2_tlist_del(&fl2, obj);
		c2_tlist_del(&fl1, obj);
		c2_tlist_del(&fl0, obj);
		c2_tlink_fini(&fl2, obj);
		c2_tlink_fini(&fl1, obj);
		c2_tlink_fini(&fl0, obj);
		obj->f_magic = 0;
	}

	for (i = 0; i < ARRAY_SIZE(head0); ++i) {
		c2_tlist_fini(&fl2, &head2[i]);
		c2_tlist_fini(&fl1, &head1[i]);
		c2_tlist_fini(&fl0, &head0[i]);
	}

	for (i = 0, b = B; i < ARRAY_SIZE(B); ++i, ++b) {
		bar_tlist_del(b);
		bar_tlink_fini(b);
	}

	bar_tlist_fini(&bhead);
}

enum {
	UB_ITER = 100000
};

static struct foo    t[UB_ITER];
static struct c2_tl  list;
static struct foo   *obj;

static void ub_init(void)
{
	int i;

	for (i = 0, obj = t; i < ARRAY_SIZE(t); ++i, ++obj)
		c2_tlink_init(&fl0, obj);
	c2_tlist_init(&fl0, &list);
}

static void ub_fini(void)
{
	int i;

	c2_tlist_fini(&fl0, &list);
	for (i = 0, obj = t; i < ARRAY_SIZE(t); ++i, ++obj)
		c2_tlink_fini(&fl0, obj);
}

static void ub_insert(int i)
{
	c2_tlist_add(&fl0, &list, &t[i]);
}

static void ub_delete(int i)
{
	c2_tlist_del(&fl0, &t[i]);
}

struct c2_ub_set c2_tlist_ub = {
	.us_name = "tlist-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ut_name = "insert",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_insert },

		{ .ut_name = "delete",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_delete },

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
