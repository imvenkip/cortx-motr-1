/* -*- C -*- */

#include <stdio.h>

extern void c2_threads_init(void);
extern void c2_threads_fini(void);

extern void test_memory(void);
extern void test_list(void);
extern void test_bitops(void);
extern void test_bitmap(void);
extern void test_refs(void);
extern void test_cache(void);
extern void test_queue(void);
extern void test_vec(void);
extern void test_thread(void);
extern void test_mutex(void);
extern void test_chan(void);

int main(int argc, char *argv[])
{
	c2_threads_init();

	test_memory();
	test_list();
	test_bitops();
	test_bitmap();
	test_refs();
	test_cache();
	test_queue();
	test_vec();
	test_thread();
	test_mutex();
	test_chan();

	c2_threads_fini();

	return 0;
}
