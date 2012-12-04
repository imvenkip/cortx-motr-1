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
 * Original author: Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 03/03/2011
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "lib/ut.h"
#include "lib/cdefs.h" /* for M0_EXPORTED */

/* These unit tests are done in the kernel */
M0_INTERNAL void test_bitmap(void);
M0_INTERNAL void test_chan(void);
M0_INTERNAL void test_cookie(void);
M0_INTERNAL void test_finject(void);
M0_INTERNAL void test_list(void);
M0_INTERNAL void test_tlist(void);
M0_INTERNAL void test_mutex(void);
M0_INTERNAL void test_queue(void);
M0_INTERNAL void test_refs(void);
M0_INTERNAL void test_rw(void);
M0_INTERNAL void test_thread(void);
M0_INTERNAL void test_time(void);
M0_INTERNAL void test_trace(void);
M0_INTERNAL void test_vec(void);
M0_INTERNAL void test_zerovec(void);
M0_INTERNAL void test_memory(void);
M0_INTERNAL void test_bob(void);
M0_INTERNAL void m0_ut_lib_buf_test(void);

const struct m0_test_suite m0_klibm0_ut = {
	.ts_name = "klibm0-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "bitmap",    test_bitmap        },
		{ "memory",    test_memory        },
		{ "bob",       test_bob           },
		{ "buf",       m0_ut_lib_buf_test },
		{ "chan",      test_chan          },
		{ "cookie",    test_cookie        },
#ifdef ENABLE_FAULT_INJECTION
		{ "finject",   test_finject       },
#endif
		{ "list",      test_list          },
		{ "tlist",     test_tlist         },
		{ "mutex",     test_mutex         },
		{ "queue",     test_queue         },
		{ "refs",      test_refs          },
		{ "rwlock",    test_rw            },
		{ "thread",    test_thread        },
		{ "time",      test_time          },
		{ "trace",     test_trace         },
		{ "vec",       test_vec           },
		{ "zerovec",   test_zerovec       },
		{ NULL,        NULL               }
	}
};
M0_EXPORTED(m0_klibm0_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
