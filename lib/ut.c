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
 * Original creation date: 07/09/2010
 */

#include <CUnit/Basic.h>
#include <CUnit/Automated.h>
#include <CUnit/Console.h>
#include <CUnit/CUCurses.h>
#include <CUnit/TestDB.h>

#include <stdlib.h>                /* system */
#include <stdio.h>                 /* asprintf */

#include "lib/assert.h"
#include "lib/thread.h"            /* LAMBDA */
#include "lib/ut.h"

/**
   @addtogroup ut
   @{
 */

int c2_uts_init(void)
{
	C2_CASSERT(CUE_SUCCESS == 0);
	return -CU_initialize_registry();
}

void c2_uts_fini(void)
{
	CU_cleanup_registry();
	C2_ASSERT(CU_get_error() == 0);
}

void c2_ut_add(const struct c2_test_suite *ts)
{
	CU_pSuite pSuite;
	int i;

	/* add a suite to the registry */
	pSuite = CU_add_suite(ts->ts_name, ts->ts_init, ts->ts_fini);
	C2_ASSERT(pSuite != NULL);

	for (i = 0; ts->ts_tests[i].t_name != NULL; i++) {
		if (CU_add_test(pSuite, ts->ts_tests[i].t_name,
				ts->ts_tests[i].t_proc) == NULL)
			break;
	}

	C2_ASSERT(ts->ts_tests[i].t_name == NULL);
}

typedef void (*ut_suite_action_t)(CU_pSuite);
typedef void (*ut_test_action_t)(CU_pSuite, CU_pTest);

static void ut_traverse_test_list(struct c2_list *list, ut_suite_action_t sa,
				  ut_test_action_t ta)
{
	struct c2_test_suite_entry *te;

	c2_list_for_each_entry(list, te, struct c2_test_suite_entry, tse_linkage) {
		CU_pTestRegistry r;
		CU_pSuite s;
		CU_pTest t;

		r = CU_get_registry();
		s = CU_get_suite_by_name(te->tse_suite_name, r);

		if (s == NULL) {
			fprintf(stderr, "Error: test suite '%s' not found\n",
					te->tse_suite_name);
			exit(EXIT_FAILURE);
		}

		if (te->tse_test_name != NULL) {
			t = CU_get_test_by_name(te->tse_test_name, s);
			if (t == NULL) {
				fprintf(stderr, "Error: test '%s:%s' not found\n",
						te->tse_suite_name,
						te->tse_test_name);
				exit(EXIT_FAILURE);
			}
			ta(s, t);
		} else {
			sa(s);
		}
	}
}

static void ut_run_basic_mode(struct c2_list *test_list,
	       struct c2_list *exclude_list)
{
	if (c2_list_is_empty(test_list)) {
		/* run all tests, except those, which present in exclude_list */
		if (!c2_list_is_empty(exclude_list)) {
			ut_suite_action_t sa = LAMBDA(void, (CU_pSuite s) {
							s->fActive = CU_FALSE;
						});
			ut_test_action_t ta = LAMBDA(void,
						     (CU_pSuite s, CU_pTest t) {
							t->fActive = CU_FALSE;
						});
			ut_traverse_test_list(exclude_list, sa, ta);
		}
		CU_basic_run_tests();
	} else {
		/*
		 * run only selected tests, which present in test_list,
		 * ignore exclude_list
		 */
		ut_suite_action_t sa = LAMBDA(void, (CU_pSuite s) {
					CU_basic_run_suite(s);
				});
		ut_test_action_t ta = LAMBDA(void, (CU_pSuite s, CU_pTest t) {
					CU_basic_run_test(s, t);
				});
		ut_traverse_test_list(test_list, sa, ta);
	}

	CU_basic_show_failures(CU_get_failure_list());
}

void c2_ut_run(enum c2_ut_run_mode mode, struct c2_list *test_list,
	       struct c2_list *exclude_list)
{
	CU_basic_set_mode(CU_BRM_VERBOSE);

	if (mode == C2_UT_AUTOMATED_MODE) {
		/* run and save results to xml */

		/*
		 * CUnit uses the name we provide as a prefix for log files.
		 * The actual log file names are PREFIX-Listing.xml and
		 * PREFIX-Results.xml, where PREFIX is what we pass to
		 * CU_set_output_filename().
		 *
		 * Because we run in the sandbox directory, which is removed by
		 * default after test execution, we need to prepend '../' to the
		 * filename, in order to store it outside of sandbox. */
		CU_set_output_filename("../C2UT");

		CU_list_tests_to_file();
		CU_automated_run_tests();
	} else {
		/* run and make console output */
		if (mode == C2_UT_BASIC_MODE) {
			ut_run_basic_mode(test_list, exclude_list);
		} else if (mode == C2_UT_ICONSOLE_MODE) {
			CU_console_run_tests();
		} else if (mode == C2_UT_ICURSES_MODE) {
			CU_console_run_tests();
		}
	}
}

void c2_ut_list(bool with_tests)
{
	CU_pTestRegistry registry;
	CU_pSuite        suite;
	CU_pTest         test;

	registry = CU_get_registry();
	C2_ASSERT(registry != NULL);

	if (registry->uiNumberOfSuites == 0) {
		fprintf(stderr, "\n%s\n", "No test suites are registered.");
		return;
	}

	printf("# YAML\n");
	printf("---\n");

	for (suite = registry->pSuite; suite != NULL; suite = suite->pNext)
		if (with_tests && suite->uiNumberOfTests != 0) {
			printf("%s:\n", suite->pName);
			for (test = suite->pTest ; test != NULL; test = test->pNext)
				printf("    - %s\n", test->pName);
		} else {
			printf("%s\n", suite->pName);
		}
}

int c2_ut_db_reset(const char *db_name)
{
        char *cmd;
	int   rc;

	rc = asprintf(&cmd, "rm -fr \"%s\"", db_name);
        if (rc < 0)
                return rc;
	rc = system(cmd);
	free(cmd);
	return rc;
}

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
