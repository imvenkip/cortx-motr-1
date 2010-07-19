/* -*- C -*- */
#include <stdlib.h>

#include <lib/memory.h>

struct test1 {
	int a;
};

void test_memory()
{
	void *ptr1;
	struct test1 *ptr2;
	size_t allocated;

	allocated = c2_allocated();
	ptr1 = c2_alloc(100);
	if (ptr1 == NULL)
		abort();

	C2_ALLOC_PTR(ptr2);
	if (ptr2 == NULL)
		abort();

	c2_free(ptr1);
	c2_free(ptr2);
	if (allocated != c2_allocated())
		abort();
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
