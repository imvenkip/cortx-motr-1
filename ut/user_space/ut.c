/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Modified by: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Modification date: 25-Mar-2013
 */

#include <stdlib.h>                /* system */
#include <stdio.h>                 /* asprintf */
#include <unistd.h>                /* dup, dup2 */

#include <CUnit/Basic.h>
#include <CUnit/Automated.h>
#include <CUnit/Console.h>
#include <CUnit/TestDB.h>
#include <CUnit/TestRun.h>

#include "lib/assert.h"            /* M0_ASSERT */
#include "lib/thread.h"            /* LAMBDA */
#include "lib/memory.h"            /* m0_allocated */
#include "lib/atomic.h"
#include "ut/ut.h"
#include "ut/cs_service.h"
#include "db/db.h"		   /* m0_dbenv_reset */


/**
 * @addtogroup ut
 *
 * @{
 */

int m0_ut_init(void)
{
	int rc;

	M0_CASSERT(CUE_SUCCESS == 0);
	rc = -CU_initialize_registry();

	return rc ?: m0_cs_default_stypes_init();
}

void m0_ut_fini(void)
{
	m0_cs_default_stypes_fini();
	CU_cleanup_registry();
	M0_ASSERT(CU_get_error() == 0);
}

M0_INTERNAL void m0_ut_add(const struct m0_test_suite *ts)
{
	CU_pSuite pSuite;
	int i;

	/* add a suite to the registry */
	pSuite = CU_add_suite(ts->ts_name, ts->ts_init, ts->ts_fini);
	M0_ASSERT(pSuite != NULL);

	for (i = 0; ts->ts_tests[i].t_name != NULL; i++) {
		if (CU_add_test(pSuite, ts->ts_tests[i].t_name,
				ts->ts_tests[i].t_proc) == NULL)
			break;
	}

	M0_ASSERT(ts->ts_tests[i].t_name == NULL);
}

typedef void (*ut_suite_action_t)(CU_pSuite);
typedef void (*ut_test_action_t)(CU_pSuite, CU_pTest);

static void ut_traverse_test_list(struct m0_list *list, ut_suite_action_t sa,
				  ut_test_action_t ta)
{
	struct m0_test_suite_entry *te;

	m0_list_for_each_entry(list, te, struct m0_test_suite_entry, tse_linkage) {
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

static void ut_run_basic_mode(struct m0_list *test_list,
	       struct m0_list *exclude_list)
{
	if (m0_list_is_empty(test_list)) {
		/* run all tests, except those, which present in exclude_list */
		if (!m0_list_is_empty(exclude_list)) {
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

static size_t used_mem_before_suite;

static void ut_suite_start_cbk(const CU_pSuite pSuite)
{
	used_mem_before_suite = m0_allocated();
}

static void ut_suite_stop_cbk(const CU_pSuite pSuite,
			      const CU_pFailureRecord pFailure)
{
	size_t used_mem_after_suite = m0_allocated();
	int    leaked_bytes = used_mem_after_suite - used_mem_before_suite;
	float  leaked;
	char   *units;
	char   *notice = "";
	int    sign = +1;

	if (leaked_bytes < 0) {
		leaked_bytes *= -1; /* make it positive */
		sign = -1;
		notice = "NOTICE: freed more memory than allocated!";
	}

	if (leaked_bytes / 1024 / 1024 ) { /* > 1 megabyte */
		leaked = leaked_bytes / 1024.0 / 1024.0;
		units = "MB";
	} else if (leaked_bytes / 1024) {  /* > 1 kilobyte */
		leaked = leaked_bytes / 1024.0;
		units = "KB";
	} else {
		leaked = leaked_bytes;
		units = "B";
	}

	printf("\n  Leaked: %.2f %s  %s", sign * leaked, units, notice);

}

static void ut_set_suite_start_stop_cbk(void)
{
	CU_set_suite_start_handler(ut_suite_start_cbk);
	CU_set_suite_complete_handler(ut_suite_stop_cbk);
}

M0_INTERNAL void m0_ut_run(struct m0_ut_run_cfg *c)
{
	ut_set_suite_start_stop_cbk();

	if (c->urc_report_exec_time)
		CU_basic_set_mode(CU_BRM_VERBOSE_TIME);
	else
		CU_basic_set_mode(CU_BRM_VERBOSE);

	if (c->urc_abort_cu_assert)
		CU_set_assert_mode(CUA_Abort);

	if (c->urc_mode == M0_UT_AUTOMATED_MODE) {
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
		CU_set_output_filename("../M0UT");

		CU_list_tests_to_file();
		CU_automated_run_tests();
	} else {
		/* run and make console output */
		if (c->urc_mode == M0_UT_BASIC_MODE) {
			ut_run_basic_mode(c->urc_test_list, c->urc_exclude_list);
		} else if (c->urc_mode == M0_UT_ICONSOLE_MODE) {
			CU_console_run_tests();
		}
	}
}

M0_INTERNAL void m0_ut_list(bool with_tests)
{
	CU_pTestRegistry registry;
	CU_pSuite        suite;
	CU_pTest         test;

	registry = CU_get_registry();
	M0_ASSERT(registry != NULL);

	if (registry->uiNumberOfSuites == 0) {
		fprintf(stderr, "\n%s\n", "No test suites are registered.");
		return;
	}

	printf("Available unit tests:\n");
	for (suite = registry->pSuite; suite != NULL; suite = suite->pNext)
		if (with_tests && suite->uiNumberOfTests != 0) {
			printf("%s:\n", suite->pName);
			for (test = suite->pTest; test != NULL;
			     test = test->pNext)
				printf("    - %s\n", test->pName);
		} else {
			printf("%s\n", suite->pName);
		}
}

M0_INTERNAL int m0_ut_db_reset(const char *db_name)
{
#if 0
        char *cmd;
	int   rc;

	rc = asprintf(&cmd, "rm -fr \"%s\"", db_name);
        if (rc < 0)
                return rc;
	rc = system(cmd);
	free(cmd);
#endif
	m0_dbenv_reset(db_name);
	return 0;
}

M0_INTERNAL void m0_stream_redirect(FILE * stream, const char *path,
				    struct m0_ut_redirect *redir)
{
	FILE *result;

	/*
	 * This solution is based on the method described in the comp.lang.c
	 * FAQ list, Question 12.34: "Once I've used freopen, how can I get the
	 * original stdout (or stdin) back?"
	 *
	 * http://c-faq.com/stdio/undofreopen.html
	 * http://c-faq.com/stdio/rd.kirby.c
	 *
	 * It's not portable and will only work on systems which support dup(2)
	 * and dup2(2) system calls (these are supported in Linux).
	 */
	redir->ur_stream = stream;
	fflush(stream);
	fgetpos(stream, &redir->ur_pos);
	redir->ur_oldfd = fileno(stream);
	redir->ur_fd = dup(redir->ur_oldfd);
	M0_ASSERT(redir->ur_fd != -1);
	result = freopen(path, "a+", stream);
	M0_ASSERT(result != NULL);
}

M0_INTERNAL void m0_stream_restore(const struct m0_ut_redirect *redir)
{
	int result;

	/*
	 * see comment in m0_stream_redirect() for detailed information
	 * about how to redirect and restore standard streams
	 */
	fflush(redir->ur_stream);
	result = dup2(redir->ur_fd, redir->ur_oldfd);
	M0_ASSERT(result != -1);
	close(redir->ur_fd);
	clearerr(redir->ur_stream);
	fsetpos(redir->ur_stream, &redir->ur_pos);
}

M0_INTERNAL bool m0_error_mesg_match(FILE * fp, const char *mesg)
{
	enum {
		MAXLINE = 1025,
	};

	char line[MAXLINE];

	M0_PRE(fp != NULL);
	M0_PRE(mesg != NULL);

	fseek(fp, 0L, SEEK_SET);
	memset(line, '\0', MAXLINE);
	while (fgets(line, MAXLINE, fp) != NULL) {
		if (strncmp(mesg, line, strlen(mesg)) == 0)
			return true;
	}
	return false;
}

/** @} end of ut group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
