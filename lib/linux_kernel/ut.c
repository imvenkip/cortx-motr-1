#include <linux/time.h>
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
 * Original creation date: 04/13/2011
 */

#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/list.h"
#include "lib/time.h"
#include "lib/ut.h"

/**
   @addtogroup ut
   @{
 */

struct test_suite_elem {
	const struct c2_test_suite *tse_suite;
	struct c2_list_link	    tse_link;
};

static int suite_ran;
static int suite_failed;
static int test_passed;
static int test_failed;
static int passed;
static int failed;
static c2_time_t started;
static struct c2_list suites;

enum {
	ONE_MILLION = 1000000ULL
};

C2_INTERNAL int c2_uts_init(void)
{
	c2_list_init(&suites);
	return 0;
}
C2_EXPORTED(c2_uts_init);

C2_INTERNAL void c2_uts_fini(void)
{
	struct c2_list_link *link;
	struct test_suite_elem *ts;

	while ((link = c2_list_first(&suites)) != NULL) {
		ts = c2_list_entry(link, struct test_suite_elem, tse_link);
		c2_list_del(&ts->tse_link);
		c2_free(ts);
	}
	c2_list_fini(&suites);
}
C2_EXPORTED(c2_uts_fini);

C2_INTERNAL void c2_ut_add(const struct c2_test_suite *ts)
{
	struct test_suite_elem *elem;

	C2_ALLOC_PTR(elem);
	elem->tse_suite = ts;
	c2_list_link_init(&elem->tse_link);
	c2_list_add_tail(&suites, &elem->tse_link);
}
C2_EXPORTED(c2_ut_add);

/**
   Generate a run summary similar in appearance to a CUnit run summary.
 */
static void uts_summary(void)
{
	int ran;
	c2_time_t now;
	c2_time_t diff;
	int64_t msec;

	now = c2_time_now();
	diff = c2_time_sub(now, started);
	msec = (c2_time_nanoseconds(diff) + ONE_MILLION / 2) / ONE_MILLION;

	printk(KERN_INFO "Run Summary:    Type  Total    Ran Passed Failed\n");
	/* initial "." keeps syslog from trimming leading spaces */
	printk(KERN_INFO ".%19s%7d%7d%7s%7d\n",
	       "suites", suite_ran, suite_ran, "n/a", suite_failed);
	ran = test_passed + test_failed;
	printk(KERN_INFO ".%19s%7d%7d%7d%7d\n",
	       "tests", ran, ran, test_passed, test_failed);
	ran = passed + failed;
	printk(KERN_INFO ".%19s%7d%7d%7d%7d\n",
	       "asserts", ran, ran, passed, failed);
	printk(KERN_INFO "Elapsed time = %4lld.%03lld seconds\n",
	       c2_time_seconds(diff), msec);
}

C2_INTERNAL void c2_ut_run(void)
{
	struct test_suite_elem *ts;
	struct c2_list_link    *pos;
	const struct c2_test   *t;
	int ret;

	suite_ran = 0;
	suite_failed = 0;
	test_passed = 0;
	test_failed = 0;
	passed = 0;
	failed = 0;
	started = c2_time_now();

	c2_list_for_each(&suites, pos) {
		bool suite_ok = true;

		ts = c2_list_entry(pos, struct test_suite_elem, tse_link);
		printk(KERN_INFO "Suite: %s\n", ts->tse_suite->ts_name);
		suite_ran++;
		if (ts->tse_suite->ts_init != NULL) {
			ret = ts->tse_suite->ts_init();
			if (ret != 0) {
				printk(KERN_ERR "Suite Prepare: failed %d\n",
				       ret);
				suite_failed++;
				continue;
			}
		}
		for (t = ts->tse_suite->ts_tests; t->t_name != NULL; ++t) {
			int oldfailed = failed;

			printk(KERN_INFO ". Test: %s...\n", t->t_name);
			t->t_proc();
			if (oldfailed == failed) {
				printk(KERN_INFO ". Test: %s...%s\n",
				       t->t_name, "passed");
				test_passed++;
			} else {
				printk(KERN_ERR ". Test: %s...%s\n",
				       t->t_name, "failed");
				test_failed++;
				suite_ok = false;
			}
		}
		if (ts->tse_suite->ts_fini != NULL) {
			ret = ts->tse_suite->ts_fini();
			if (ret != 0) {
				printk(KERN_ERR "Suite Cleanup: failed %d\n",
				       ret);
				suite_failed++;
				continue;
			}
		}
		if (!suite_ok)
			suite_failed++;
	}

	uts_summary();
}
C2_EXPORTED(c2_ut_run);

C2_INTERNAL bool c2_ut_assertimpl(bool c, int lno, const char *str_c,
				  const char *file)
{
	if (!c) {
		printk(KERN_ERR "Unit test assertion failed: %s at %s:%d\n",
		       str_c, file, lno);
		failed++;
	} else
		passed++;
	return c;
}
C2_EXPORTED(c2_ut_assertimpl);

/** @} end of ut group. */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
