/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 04/12/2011
 */

#include <linux/module.h>

#include "lib/thread.h"       /* M0_THREAD_INIT */
#include "ut/ut.h"            /* m0_ut_add */


MODULE_AUTHOR("Xyratex International");
MODULE_DESCRIPTION("Mero Unit Test Module");
MODULE_LICENSE("GPL"); /* should match license of m0mero.ko module */

static char *tests;
module_param(tests, charp, S_IRUGO);
MODULE_PARM_DESC(tests, " list of tests to run in format"
		 " 'suite[:test][,suite[:test]]'");

static char *exclude;
module_param(exclude, charp, S_IRUGO);
MODULE_PARM_DESC(exclude, " list of tests to exclude in format"
		 " 'suite[:test][,suite[:test]]'");

/* sort test suites in alphabetic order */
extern struct m0_ut_suite m0_klibm0_ut; /* test lib first */
extern struct m0_ut_suite m0_addb_ut;
extern struct m0_ut_suite be_ut;
extern struct m0_ut_suite buffer_pool_ut;
extern struct m0_ut_suite bulkio_client_ut;
extern struct m0_ut_suite m0_net_bulk_if_ut;
extern struct m0_ut_suite m0_net_bulk_mem_ut;
extern struct m0_ut_suite m0_net_lnet_ut;
extern struct m0_ut_suite m0_net_test_ut;
extern struct m0_ut_suite m0_net_tm_prov_ut;
extern struct m0_ut_suite conn_ut;
extern struct m0_ut_suite dtm_dtx_ut;
extern struct m0_ut_suite dtm_nucleus_ut;
extern struct m0_ut_suite dtm_transmit_ut;
extern struct m0_ut_suite file_io_ut;
extern struct m0_ut_suite frm_ut;
extern struct m0_ut_suite layout_ut;
extern struct m0_ut_suite packet_encdec_ut;
extern struct m0_ut_suite reqh_service_ut;
extern struct m0_ut_suite rpc_mc_ut;
extern struct m0_ut_suite rm_ut;
extern struct m0_ut_suite session_ut;
extern struct m0_ut_suite sm_ut;
extern struct m0_ut_suite stob_ut;
extern struct m0_ut_suite xcode_ut;
extern struct m0_ut_suite di_ut;

extern struct m0_ut_suite m0_loop_ut; /* m0loop driver */

static struct m0_thread ut_thread;

static void run_kernel_ut(int ignored)
{
        printk(KERN_INFO "Mero Kernel Unit Test\n");

	/* sort test suites in alphabetic order */
	m0_ut_add(&m0_klibm0_ut);  /* test lib first */
	m0_ut_add(&m0_addb_ut);
	m0_ut_add(&di_ut);
	m0_ut_add(&file_io_ut);
	m0_ut_add(&be_ut);
	m0_ut_add(&buffer_pool_ut);
	m0_ut_add(&bulkio_client_ut);
	m0_ut_add(&m0_loop_ut);
	m0_ut_add(&m0_net_bulk_if_ut);
	m0_ut_add(&m0_net_bulk_mem_ut);
	m0_ut_add(&m0_net_lnet_ut);
	m0_ut_add(&m0_net_test_ut);
	m0_ut_add(&m0_net_tm_prov_ut);
	m0_ut_add(&conn_ut);
	m0_ut_add(&dtm_nucleus_ut);
	m0_ut_add(&dtm_transmit_ut);
	m0_ut_add(&dtm_dtx_ut);
	m0_ut_add(&frm_ut);
	m0_ut_add(&layout_ut);
	m0_ut_add(&packet_encdec_ut);
	m0_ut_add(&reqh_service_ut);
	m0_ut_add(&rm_ut);
	m0_ut_add(&rpc_mc_ut);
	m0_ut_add(&session_ut);
	m0_ut_add(&sm_ut);
	m0_ut_add(&stob_ut);
	m0_ut_add(&xcode_ut);

	m0_ut_run();
}

static int __init m0_ut_module_init(void)
{
	struct m0_ut_cfg cfg = {
		.uc_run_list     = tests,
		.uc_exclude_list = exclude,
	};
	int rc;

	rc = m0_ut_init(&cfg);
	M0_ASSERT(rc == 0);

	rc = M0_THREAD_INIT(&ut_thread, int, NULL,
		            &run_kernel_ut, 0, "m0kut");
	M0_ASSERT(rc == 0);

	return rc;
}

static void __exit m0_ut_module_fini(void)
{
	m0_thread_join(&ut_thread);
	m0_ut_fini();
}

module_init(m0_ut_module_init)
module_exit(m0_ut_module_fini)

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
