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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 10/17/2011
 */

#include "lib/ut.h"
#include "lib/rbtree.h"
#include "lib/assert.h"

#include <stdlib.h>	/* rand() */

struct node {
	struct c2_rbtree_link	n_linkage;
	int			n_val;
};

int cmp_int(void *_a, void *_b)
{
	int a = * (int *) _a;
	int b = * (int *) _b;

	return a == b ? 0 : a < b ? -1 : 1;
}

enum {
	START_SIZE = 0,
	MAX_SIZE = 256,
	ITERATIONS = 256
};

static void test_rbtree_array_size(int *data, int size)
{
	struct c2_rbtree t;
	static struct node m[MAX_SIZE];
	struct node *n;
	struct node *p;
	struct c2_rbtree_link *rl;
	int i;

	C2_ASSERT(size <= MAX_SIZE);

	c2_rbtree_init(&t, &cmp_int, offsetof(struct node, n_val) - offsetof(struct node, n_linkage));
	C2_UT_ASSERT(c2_rbtree_is_empty(&t));
	C2_UT_ASSERT(c2_rbtree_min(&t) == NULL);

	for (i = 0; i < size; ++i) {
		m[i].n_val = data[i];
		c2_rbtree_link_init(&m[i].n_linkage);
	}

	for (i = 0; i < size; ++i) {
		C2_UT_ASSERT(c2_rbtree_find(&t, &data[i]) == NULL);
		C2_UT_ASSERT(!c2_rbtree_remove(&t, &m[i].n_linkage));
		C2_UT_ASSERT(c2_rbtree_insert(&t, &m[i].n_linkage));
		C2_UT_ASSERT(c2_rbtree_find(&t, &data[i]) == &m[i].n_linkage);
		C2_UT_ASSERT(!c2_rbtree_insert(&t, &m[i].n_linkage));
		C2_UT_ASSERT(!c2_rbtree_is_empty(&t));
	}

	rl = NULL;
	if (size != 0)
		C2_UT_ASSERT((rl = c2_rbtree_min(&t)) != NULL);
	p = NULL;
	i = 0;
	for (; rl != NULL; rl = c2_rbtree_next(rl)) {
		n = container_of(rl, struct node, n_linkage);
		if (p != NULL)
			C2_UT_ASSERT(p->n_val < n->n_val);
		p = n;
		++i;
	}
	C2_UT_ASSERT(i == size);

	for (i = 0; i < size; ++i) {
		C2_UT_ASSERT(c2_rbtree_remove(&t, &m[i].n_linkage));
		C2_UT_ASSERT(!c2_rbtree_remove(&t, &m[i].n_linkage));
		c2_rbtree_link_fini(&m[i].n_linkage);
	}

	c2_rbtree_fini(&t);
}

static void test_rbtree_array(int *data, int start_size, int max_size)
{
	int i;

	for (i = start_size; i <= max_size; i = i == 0 ? 1 : i*2)
		test_rbtree_array_size(data, i);
}

void test_rbtree(void)
{
	static int data[MAX_SIZE];
	int i;
	int j;
	unsigned int seed;

	for (i = 0; i < MAX_SIZE; ++i)
		data[i] = i;
	test_rbtree_array(data, START_SIZE, MAX_SIZE);

	for (i = 0; i < MAX_SIZE; ++i)
		data[i] = MAX_SIZE - i;
	test_rbtree_array(data, START_SIZE, MAX_SIZE);

	for (i = 0; i < MAX_SIZE; ++i)
		data[i] = (i * 2) % (MAX_SIZE + 1);
	test_rbtree_array(data, START_SIZE, MAX_SIZE);

	for (seed = 0; seed < ITERATIONS; ++seed) {
		srand(seed);
		// fill data[] with random values without duplicates
		for (i = 0; i < MAX_SIZE; ++i) {
			data[i] = rand();
			for (j = 0; j < i; ++j)
				if (data[i] == data[j]) {
					--i;
					break;
				}
		}
		test_rbtree_array(data, START_SIZE, MAX_SIZE);
	}
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
