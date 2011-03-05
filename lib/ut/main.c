/* -*- C -*- */

#include "lib/ut.h"

/* sort test suites in alphabetic order */
extern void test_atomic(void);
extern void test_bitmap(void);
extern void test_chan(void);
extern void test_getopts(void);
extern void test_list(void);
extern void test_memory(void);
extern void test_mutex(void);
extern void test_queue(void);
extern void test_refs(void);
extern void test_thread(void);
extern void test_time(void);
extern void test_timer(void);
extern void test_trace(void);
extern void test_vec(void);

const struct c2_test_suite libc2_ut = {
	.ts_name = "libc2-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "atomic",   test_atomic    },
		{ "bitmap",   test_bitmap    },
		{ "chan",     test_chan      },
		{ "getopts",  test_getopts   },
		{ "list",     test_list      },
		{ "memory",   test_memory    },
		{ "mutex",    test_mutex     },
		{ "queue",    test_queue     },
		{ "refs",     test_refs      },
		{ "thread",   test_thread    },
		{ "time",     test_time      },
		{ "timer",    test_timer     },
		{ "trace",    test_trace     },
		{ "vec",      test_vec       },
		{ NULL,       NULL           }
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
