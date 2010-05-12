/* -*- C -*- */

#include <stdio.h>

extern void c2_threads_init(void);
extern void c2_threads_fini(void);

extern void test_memory(void);
extern void test_list(void);
extern void test_bitops(void);
extern void test_bitmap(void);
extern void test_refs(void);
extern void test_queue(void);
extern void test_vec(void);
extern void test_thread(void);

int main(int argc, char *argv[])
{
	c2_threads_init();

	test_memory();
	test_list();
	test_bitops();
	test_bitmap();
	test_refs();
	test_queue();
	test_vec();
	test_thread();

	c2_threads_fini();

	return 0;
}
