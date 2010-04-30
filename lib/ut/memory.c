/* -*- C -*- */
#include <stdlib.h>

#include "lib/memory.h"


struct test1 {
	int a;
};

void test_memory()
{
	void *ptr1;
	struct test1 *ptr2;

	ptr1 = c2_alloc(100);
	if (ptr1 == NULL)
		abort();

	C2_ALLOC_PTR(ptr2);
	if (ptr2 == NULL)
		abort();

	c2_free(ptr1, 100);
	C2_FREE_PTR(ptr2);
}

