/* -*- C -*- */

#include <lib/ut.h>

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
extern void test_trace(void);

const struct c2_test_suite libc2_ut = {
	.ts_name = "libc2-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "memory", test_memory },
		{ "list", test_list },
		{ "refs", test_refs },
		{ "cache", test_cache },
		{ "queue", test_queue },
		{ "vec", test_vec },
		{ "thread", test_thread },
		{ "mutex", test_mutex },
		{ "chan", test_chan },
		{ "atomic", test_atomic },
		{ "trace", test_trace },
		{ NULL, NULL }
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
