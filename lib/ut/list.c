/* -*- C -*- */

#include "lib/ub.h"
#include "lib/ut.h"
#include "lib/cdefs.h"
#include "lib/memory.h"
#include "lib/list.h"

struct test1 {
	struct c2_list_link	t_link;
	int	c;
};

void test_list(void)
{
	struct test1	t1, t2, t3, t4;
	struct c2_list_link	*pos;
	struct c2_list	test_head;
	struct test1 *p;
	int t_sum;

	c2_list_init(&test_head);

	c2_list_link_init(&t1.t_link);
	t1.c = 5;
	c2_list_link_init(&t2.t_link);
	t2.c = 10;
	c2_list_link_init(&t3.t_link);
	t3.c = 15;

	C2_UT_ASSERT(!c2_list_contains(&test_head, &t1.t_link));
	C2_UT_ASSERT(!c2_list_contains(&test_head, &t2.t_link));
	C2_UT_ASSERT(!c2_list_contains(&test_head, &t3.t_link));

	c2_list_add(&test_head, &t1.t_link);
	c2_list_add_tail(&test_head, &t2.t_link);
	c2_list_add(&test_head, &t3.t_link);

	C2_UT_ASSERT(c2_list_contains(&test_head, &t1.t_link));
	C2_UT_ASSERT(c2_list_contains(&test_head, &t2.t_link));
	C2_UT_ASSERT(c2_list_contains(&test_head, &t3.t_link));

	c2_list_for_each(&test_head, pos) {
		p = c2_list_entry(pos,struct test1, t_link);
	}

	C2_UT_ASSERT(c2_list_contains(&test_head, &t2.t_link));
	C2_UT_ASSERT(!c2_list_contains(&test_head, &t4.t_link));

	c2_list_del(&t2.t_link);

	C2_UT_ASSERT( c2_list_contains(&test_head, &t1.t_link));
	C2_UT_ASSERT(!c2_list_contains(&test_head, &t2.t_link));
	C2_UT_ASSERT( c2_list_contains(&test_head, &t3.t_link));

	t_sum = 0;
	c2_list_for_each(&test_head, pos) {
		p = c2_list_entry(pos,struct test1, t_link);
		t_sum += p->c;
	}
	C2_UT_ASSERT(t_sum == 20);

	c2_list_del(&t1.t_link);

	C2_UT_ASSERT(!c2_list_contains(&test_head, &t1.t_link));
	C2_UT_ASSERT(!c2_list_contains(&test_head, &t2.t_link));
	C2_UT_ASSERT( c2_list_contains(&test_head, &t3.t_link));

	t_sum = 0;
	c2_list_for_each_entry(&test_head, p, struct test1, t_link) {
		t_sum += p->c;
	}
	C2_UT_ASSERT(t_sum == 15);

	c2_list_del(&t3.t_link);

	C2_UT_ASSERT(!c2_list_contains(&test_head, &t1.t_link));
	C2_UT_ASSERT(!c2_list_contains(&test_head, &t2.t_link));
	C2_UT_ASSERT(!c2_list_contains(&test_head, &t3.t_link));

	t_sum = 0;
	c2_list_for_each_entry(&test_head, p, struct test1, t_link) {
		t_sum += p->c;
	}
	C2_UT_ASSERT(t_sum == 0);

	C2_UT_ASSERT(c2_list_is_empty(&test_head));
	c2_list_fini(&test_head);
}

enum {
	UB_ITER = 100000
};

static struct test1 t[UB_ITER];
static struct c2_list list;

static void ub_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(t); ++i)
		c2_list_link_init(&t[i].t_link);
	c2_list_init(&list);
}

static void ub_fini(void)
{
	int i;

	c2_list_fini(&list);
	for (i = 0; i < ARRAY_SIZE(t); ++i)
		c2_list_link_fini(&t[i].t_link);
}

static void ub_insert(int i)
{
	c2_list_add(&list, &t[i].t_link);
}

static void ub_delete(int i)
{
	c2_list_del(&t[i].t_link);
}

struct c2_ub_set c2_list_ub = {
	.us_name = "list-ub",
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
