/* -*- C -*- */

#include <stdio.h>
#include <stdlib.h>

#include "lib/c2list.h"

struct test1 {
	struct c2_list_link	t_link;
	int	c;
};

void test_list(void)
{
	struct test1	t1, t2, t3;
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

	c2_list_add(&test_head, &t1.t_link);
	c2_list_add(&test_head, &t2.t_link);
	c2_list_add(&test_head, &t3.t_link);

	c2_list_for_each(&test_head, pos) {
		p = c2_list_entry(pos,struct test1, t_link);
		printf("%d ", p->c);
	}
	printf("\n");

	c2_list_del(&t2.t_link);
	t_sum = 0;
	c2_list_for_each(&test_head, pos) {
		p = c2_list_entry(pos,struct test1, t_link);
		t_sum += p->c;
	}
	if (t_sum != 20)
		abort();

	c2_list_del(&t1.t_link);
	t_sum = 0;
	c2_list_for_each_entry(&test_head, p, struct test1, t_link) {
		t_sum += p->c;
	}
	if (t_sum != 15)
		abort();

	c2_list_del(&t3.t_link);
	t_sum = 0;
	c2_list_for_each_entry(&test_head, p, struct test1, t_link) {
		t_sum += p->c;
	}
	if (t_sum != 0)
		abort();

	c2_list_fini(&test_head);
}


