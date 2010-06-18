/* -*- C -*- */
#include <stdio.h>

#include <colibri/init.h>

extern void test_memory(void);
extern void test_list(void);
extern void test_refs(void);
extern void test_cache(void);
extern void test_queue(void);
extern void test_vec(void);
extern void test_thread(void);
extern void test_mutex(void);
extern void test_chan(void);
extern void test_atomic(void);

int main(int argc, char *argv[])
{
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	c2_init();

	test_memory();
	test_list();
	test_refs();
	test_cache();
	test_queue();
	test_vec();
	test_thread();
	test_mutex();
	test_chan();
	test_atomic();

	c2_fini();

	return 0;
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
