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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 05/23/2010
 */

#include "lib/ut.h"

/* sort test suites in alphabetic order */
extern void m0_test_lib_uuid(void);
extern void m0_ut_lib_buf_test(void);
extern void test_atomic(void);
extern void test_bitmap(void);
extern void test_bob(void);
extern void test_chan(void);
extern void test_cookie(void);
extern void test_finject(void);
extern void test_getopts(void);
extern void test_list(void);
extern void test_memory(void);
extern void m0_test_misc(void);
extern void test_mutex(void);
extern void test_processor(void);
extern void test_queue(void);
extern void test_refs(void);
extern void test_rw(void);
extern void test_thread(void);
extern void test_time(void);
extern void test_timer(void);
extern void test_tlist(void);
extern void test_trace(void);
extern void test_vec(void);
extern void test_zerovec(void);

const struct m0_test_suite libm0_ut = {
	.ts_name = "libm0-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "atomic",    test_atomic        },
		{ "bitmap",    test_bitmap        },
		{ "bob",       test_bob           },
		{ "buf",       m0_ut_lib_buf_test },
		{ "chan",      test_chan          },
		{ "cookie",    test_cookie        },
#ifdef ENABLE_FAULT_INJECTION
		{ "finject",   test_finject       },
#endif
		{ "getopts",   test_getopts       },
		{ "list",      test_list          },
		{ "memory",    test_memory        },
		{ "misc",      m0_test_misc	  },
		{ "mutex",     test_mutex         },
		{ "rwlock",    test_rw            },
		{ "processor", test_processor     },
		{ "queue",     test_queue         },
		{ "refs",      test_refs          },
		{ "thread",    test_thread        },
		{ "time",      test_time          },
		{ "timer",     test_timer         },
		{ "tlist",     test_tlist         },
		{ "trace",     test_trace         },
		{ "uuid",      m0_test_lib_uuid   },
		{ "vec",       test_vec           },
		{ "zerovec",   test_zerovec       },
		{ NULL,        NULL               }
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
