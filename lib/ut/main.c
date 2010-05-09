/* -*- C -*- */

#include <stdio.h>

extern void test_memory(void);
extern void test_list(void);
extern void test_bitops(void);
extern void test_bitmap(void);
extern void test_refs(void);
extern void test_cache(void);


int main(int argc, char *argv[])
{
	test_memory();
	test_list();
	test_bitops();
	test_bitmap();
	test_refs();
	test_cache();

	return 0;
}
