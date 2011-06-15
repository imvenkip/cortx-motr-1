#include <linux/module.h>
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
#include <linux/kernel.h>
#include <linux/init.h>

#include "lib/ut.h"
#include "lib/cdefs.h" /* for C2_EXPORTED */

MODULE_AUTHOR("Xyratex International");
MODULE_DESCRIPTION("Colibri Library");
MODULE_LICENSE("proprietary");

int init_module(void)
{
	return 0;
}

void cleanup_module(void)
{
}

/* These unit tests are done in the kernel */
extern void test_bitmap(void);
extern void test_chan(void);
extern void test_rw(void);
extern void test_thread(void);

const struct c2_test_suite c2_klibc2_ut = {
	.ts_name = "klibc2-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "bitmap",    test_bitmap    },
		{ "chan",      test_chan      },
		{ "rwlock",    test_rw        },
		{ "thread",    test_thread    },
		{ NULL,        NULL           }
	}
};
C2_EXPORTED(c2_klibc2_ut);

