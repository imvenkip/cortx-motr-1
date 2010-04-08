/* -*- C -*- */

#include <stdio.h>
#include <stdlib.h>

#include "lib/refs.h"

struct test_struct {
	struct c2_ref	ref;
};

static int free_done = 0;

void test_destructor(struct c2_ref *r)
{
	struct test_struct *t;

	t = container_of(r, struct test_struct, ref);

	free(t);
	free_done = 1;
	printf("free done\n");
}

void test_refs(void)
{
	struct test_struct *t;

	t = malloc(sizeof(struct test_struct));
	if (!t)
		abort();

	c2_ref_init(&t->ref, 1, test_destructor);
	
	c2_ref_get(&t->ref);
	c2_ref_put(&t->ref);
	c2_ref_put(&t->ref);

	if (!free_done)
		abort();
}