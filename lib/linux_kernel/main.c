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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "lib/ut.h"
#include "lib/cdefs.h" /* for C2_EXPORTED */

/* These unit tests are done in the kernel */
extern void test_bitmap(void);
extern void test_chan(void);
extern void test_finject(void);
extern void test_list(void);
extern void test_tlist(void);
extern void test_mutex(void);
extern void test_queue(void);
extern void test_refs(void);
extern void test_rw(void);
extern void test_thread(void);
extern void test_time(void);
extern void test_trace(void);
extern void test_vec(void);
extern void test_zerovec(void);
extern void test_memory(void);
extern void test_bob(void);

const struct c2_test_suite c2_klibc2_ut = {
	.ts_name = "klibc2-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "bitmap",    test_bitmap    },
		{ "memory",    test_memory    },
		{ "bob",       test_bob       },
		{ "chan",      test_chan      },
#ifdef ENABLE_FAULT_INJECTION
		{ "finject",   test_finject   },
#endif
		{ "list",      test_list      },
		{ "tlist",     test_tlist     },
		{ "mutex",     test_mutex     },
		{ "queue",     test_queue     },
		{ "refs",      test_refs      },
		{ "rwlock",    test_rw        },
		{ "thread",    test_thread    },
		{ "time",      test_time      },
		{ "trace",     test_trace     },
		{ "vec",       test_vec       },
		{ "zerovec",   test_zerovec   },
		{ NULL,        NULL           }
	}
};
C2_EXPORTED(c2_klibc2_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
