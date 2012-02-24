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
 * Original author: Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 04/12/2011
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "lib/assert.h"
#include "lib/thread.h"
#include "lib/ut.h"

MODULE_AUTHOR("Xyratex International");
MODULE_DESCRIPTION("Colibri Unit Test Module");
MODULE_LICENSE("proprietary");

/* UT suites */
extern const struct c2_test_suite c2_klibc2_ut;
extern const struct c2_test_suite c2_net_bulk_if_ut;
extern const struct c2_test_suite c2_net_bulk_mem_ut;
extern const struct c2_test_suite c2_net_bulk_sunrpc_ut;
extern const struct c2_test_suite c2_net_ksunrpc_ut;
extern const struct c2_test_suite c2_net_lnet_ut;
extern const struct c2_test_suite buffer_pool_ut;
extern const struct c2_test_suite xcode_ut;

static struct c2_thread ut_thread;

static void run_kernel_ut(int ignored)
{
        printk(KERN_INFO "Colibri Kernel Unit Test\n");

	c2_uts_init();
	c2_ut_add(&c2_klibc2_ut);
	c2_ut_add(&c2_net_bulk_if_ut);
	c2_ut_add(&c2_net_bulk_mem_ut);
	c2_ut_add(&c2_net_bulk_sunrpc_ut);
	c2_ut_add(&c2_net_ksunrpc_ut);
	c2_ut_add(&c2_net_lnet_ut);
	c2_ut_add(&buffer_pool_ut);
	c2_ut_add(&xcode_ut);
	c2_ut_run();
	c2_uts_fini();
}

static int __init c2_ut_module_init(void)
{
	int rc;

	rc = C2_THREAD_INIT(&ut_thread, int, NULL,
		            &run_kernel_ut, 0, "run_kernel_ut");
	C2_ASSERT(rc == 0);

	return rc;
}

static void __exit c2_ut_module_fini(void)
{
	c2_thread_join(&ut_thread);
}

module_init(c2_ut_module_init)
module_exit(c2_ut_module_fini)

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
