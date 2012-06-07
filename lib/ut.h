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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 07/09/2010
 */

#pragma once

#ifndef __COLIBRI_LIB_UT_H__
#define __COLIBRI_LIB_UT_H__

#ifndef __KERNEL__
# include <stdbool.h>     /* bool */
# include <stdio.h>       /* FILE, fpos_t */
# include <CUnit/Basic.h>
#else
# include "lib/types.h"
#endif

#include "lib/list.h" /* c2_list_link, c2_list */

/**
   @defgroup ut Unit testing.

   CUnit based unit testing support

   @{
 */

#ifndef __KERNEL__
# define C2_UT_ASSERT(a)	({ CU_ASSERT(a) })
# define C2_UT_PASS(m)		({ CU_PASS(m) })
# define C2_UT_FAIL(m)		({ CU_FAIL(m) })
#else
# define C2_UT_ASSERT(a)	c2_ut_assertimpl((a), __LINE__, #a, __FILE__)
# define C2_UT_PASS(m)		c2_ut_assertimpl(true, __LINE__, m, __FILE__)
# define C2_UT_FAIL(m)		c2_ut_assertimpl(false, __LINE__, m, __FILE__)
#endif

/**
   structure to define test in test suite.
 */
struct c2_test {
	/**
	   name of the test, must be unique.
	 */
	const char *t_name;
	/**
	   pointer to testing procedure
	 */
	void      (*t_proc)(void);
};

struct c2_test_suite {
	/**
	   name of a suite
	*/
	const char           *ts_name;
	/**
	   function to prepare tests in suite

	   @warning it's not allowed to use any of CUnit assertion macros, like
		    CU_ASSERT or C2_UT_ASSERT, in this function because it will
		    lead to a crash; use C2_ASSERT instead if required.
	 */
	int                 (*ts_init)(void);
	/**
	   function to free resources after tests run

	   @warning it's not allowed to use any of CUnit assertion macros, like
		    CU_ASSERT or C2_UT_ASSERT, in this function because it will
		    lead to a crash; use C2_ASSERT instead if required.
	 */
	int                 (*ts_fini)(void);
	/**
	   tests in suite
	 */
	const struct c2_test  ts_tests[];
};

struct c2_test_suite_entry {
	struct c2_list_link  tse_linkage;
	const char           *tse_suite_name;
	const char           *tse_test_name;
};

/**
   Global constructor for unit tests.
 */
int c2_uts_init(void);

/**
   Global destructor for unit tests.
 */
void c2_uts_fini(void);

/**
 add test site into global pool.
 if adding test suite failed application is aborted.

 @param ts pointer to test suite

 */
void c2_ut_add(const struct c2_test_suite *ts);

/**
   CUnit user interfaces
 */
enum c2_ut_run_mode {
	C2_UT_KERNEL_MODE,    /** A stub for kernel version of c2_ut_run() */
	C2_UT_BASIC_MODE,     /** Basic CUnit interface with console output */
	C2_UT_ICONSOLE_MODE,  /** Interactive CUnit console interface */
	C2_UT_AUTOMATED_MODE, /** Automated CUnit interface with xml output */
};

/**
   Configuration parameters for c2_ut_run()
 */
struct c2_ut_run_cfg {
	/** CUnit interface mode */
	enum c2_ut_run_mode  urc_mode;
	/** if true, then set CUnit's assert mode to CUA_Abort */
	bool                 urc_abort_cu_assert;
	/** if true, then execution time is reported for each test */
	bool                 urc_report_exec_time;
	/**
	 * list of tests/suites to run, it can be empty, which means to run
	 * all the tests
	 */
	struct c2_list       *urc_test_list;
	/** list of tests/suites to exclude from running, it also can be empty */
	struct c2_list       *urc_exclude_list;
};

#ifndef __KERNEL__
/**
   run tests
 */
void c2_ut_run(struct c2_ut_run_cfg *c);
#else
void c2_ut_run(void);
#endif

/**
 print all available test suites in YAML format to STDOUT

 @param with_tests - if true, then all tests of each suite are printed in
                     addition

 @return NONE
 */
void c2_ut_list(bool with_tests);

/**
 commonly used test database reset function
 */
int c2_ut_db_reset(const char *db_name);

#ifdef __KERNEL__
/**
   Implements UT assert logic in the kernel, where there is no CUnit.
   Similar to CUnit UT assert, this logs failures but does not terminate
   the process.
   @param c the result of the boolean condition, evaluated by caller
   @param lno line number of the assertion, eg __LINE__
   @param str_c string representation of the condition, c
   @param file path of the file, eg __FILE__
 */
bool c2_ut_assertimpl(bool c, int lno, const char *str_c, const char *file);
#endif

#ifndef __KERNEL__
struct c2_ut_redirect {
	FILE  *ur_stream;
	int    ur_oldfd;
	int    ur_fd;
	fpos_t ur_pos;
};

/**
 * Associates one of the standard streams (stdin, stdout, stderr) with a file
 * pointed by 'path' argument.
 */
void c2_stream_redirect(FILE *stream, const char *path,
			struct c2_ut_redirect *redir);

/**
 * Restores standard stream from file descriptor and stream position, which were
 * saved earlier by c2_stream_redirect().
 */
void c2_stream_restore(const struct c2_ut_redirect *redir);

/**
 * Checks if a text file contains the specified string.
 *
 * @param fp   - a file, which is searched for a string
 * @param mesg - a string to search for
 */
bool c2_error_mesg_match(FILE *fp, const char *mesg);
#endif

/** @} end of ut group. */

/* __COLIBRI_LIB_UT_H__ */



#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
