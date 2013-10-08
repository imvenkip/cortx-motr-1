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

#include <linux/string.h>  /* strlen */
#include <linux/time.h>

#include "lib/arith.h"     /* max_check */
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/list.h"
#include "lib/time.h"
#include "ut/ut.h"
#include "ut/cs_service.h"

/**
   @addtogroup ut
   @{
 */

struct test_suite_elem {
	const struct m0_test_suite *tse_suite;
	struct m0_list_link	    tse_link;
};

static int suite_ran;
static int suite_failed;
static int test_passed;
static int test_failed;
static int passed;
static int failed;
static m0_time_t started;
static struct m0_list suites;

enum {
	ONE_MILLION = 1000000ULL
};

M0_INTERNAL int m0_ut_init(void)
{
	m0_list_init(&suites);
	return m0_cs_default_stypes_init();
}
M0_EXPORTED(m0_ut_init);

M0_INTERNAL void m0_ut_fini(void)
{
	struct m0_list_link *link;
	struct test_suite_elem *ts;

	while ((link = m0_list_first(&suites)) != NULL) {
		ts = m0_list_entry(link, struct test_suite_elem, tse_link);
		m0_list_del(&ts->tse_link);
		m0_free(ts);
	}
	m0_cs_default_stypes_fini();
	m0_list_fini(&suites);
}
M0_EXPORTED(m0_ut_fini);

M0_INTERNAL void m0_ut_add(const struct m0_test_suite *ts)
{
	struct test_suite_elem *elem;

	M0_ALLOC_PTR(elem);
	elem->tse_suite = ts;
	m0_list_link_init(&elem->tse_link);
	m0_list_add_tail(&suites, &elem->tse_link);
}
M0_EXPORTED(m0_ut_add);

static int decimal_width(int n)
{
	int w = 1;  /* at least 1 decimal digit */
	int ref;

	if (n < 0) {
		w++;  /* one character more for sign */
		n = -n;
	}
	for (ref = 10; ref <= n; ref *= 10) {
		w++;
		if (ref * 10 < ref)  /* overflow check */
			break;
	}

	return w;
}

/**
   Generate a run summary similar in appearance to a CUnit run summary.
 */
static void uts_summary(void)
{
	m0_time_t now;
	m0_time_t diff;
	int64_t msec;
	int ran_w;     /* Ran/Total column width */
	int passed_w;  /* Passed column width */
	int failed_w;  /* Failed column width */
	int test_ran = test_passed + test_failed;
	int ran = passed + failed;

	now = m0_time_now();
	diff = m0_time_sub(now, started);
	msec = (m0_time_nanoseconds(diff) + ONE_MILLION / 2) / ONE_MILLION;

	ran_w    = max_check(max_check((int)strlen("Total"),
				       decimal_width(suite_ran)),
			     max_check(decimal_width(test_ran),
				       decimal_width(ran))
			    ) + 1;  /* +1 char for space between columns */
	passed_w = max_check((int)strlen("Passed"),
			     max_check(decimal_width(test_passed),
				       decimal_width(passed))) + 1;
	failed_w = max_check((int)strlen("Failed"),
			     max_check(decimal_width(test_failed),
				       decimal_width(failed))) + 1;
	printk(KERN_INFO "Run Summary:    Type%*s%*s%*s%*s\n",
	       ran_w, "Total", ran_w, "Ran", passed_w, "Passed", failed_w,
	       "Failed");
	/* initial "." keeps syslog from trimming leading spaces */
	printk(KERN_INFO ".%19s%*d%*d%*s%*d\n",
	       "suites", ran_w, suite_ran, ran_w, suite_ran, passed_w, "n/a",
	       failed_w, suite_failed);
	printk(KERN_INFO ".%19s%*d%*d%*d%*d\n",
	       "tests", ran_w, test_ran, ran_w, test_ran, passed_w,
	       test_passed, failed_w, test_failed);
	printk(KERN_INFO ".%19s%*d%*d%*d%*d\n",
	       "asserts", ran_w, ran, ran_w, ran, passed_w, passed, failed_w,
	       failed);
	printk(KERN_INFO "Elapsed time = %4lld.%03lld seconds\n",
	       m0_time_seconds(diff), msec);
}

M0_INTERNAL void m0_ut_run(void)
{
	struct test_suite_elem *ts;
	struct m0_list_link    *pos;
	const struct m0_test   *t;
	int ret;

	suite_ran = 0;
	suite_failed = 0;
	test_passed = 0;
	test_failed = 0;
	passed = 0;
	failed = 0;
	started = m0_time_now();

	m0_list_for_each(&suites, pos) {
		bool suite_ok = true;

		ts = m0_list_entry(pos, struct test_suite_elem, tse_link);
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
M0_EXPORTED(m0_ut_run);

M0_INTERNAL bool m0_ut_assertimpl(bool c, int lno, const char *str_c,
				  const char *file, const char *func,
				  bool panic)
{
	static char buf[1024];

	if (!c) {
		if (panic) {
			snprintf(buf, sizeof buf,
				"Unit test assertion failed: %s", str_c);
			m0_panic(&(struct m0_panic_ctx){
					.pc_expr = buf,  .pc_func   = func,
					.pc_file = file, .pc_lineno = lno,
					.pc_fmt  = NULL });
		}
		pr_err("Unit test assertion failed: %s at %s:%d\n",
		       str_c, file, lno);
		failed++;
	} else
		passed++;
	return c;
}
M0_EXPORTED(m0_ut_assertimpl);

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
