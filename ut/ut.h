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
 * Modification date: 03/25/2013
 */

#pragma once

#ifndef __MERO_UT_UT_H__
#define __MERO_UT_UT_H__

#ifndef __KERNEL__
# include <stdbool.h>     /* bool */
# include <stdio.h>       /* FILE, fpos_t */
# include <CUnit/Basic.h>
#else
# include "lib/types.h"
#endif

#include "lib/list.h"     /* m0_list_link, m0_list */
#include "fop/fom.h"

/**
   @defgroup ut Mero UT library
   @brief Common unit test library

   CUnit based unit testing support

   The intent of this library is to include all code, which could be potentially
   useful for several UTs and thus can be shared, avoiding duplication of
   similar code.

   @{
*/

#ifndef __KERNEL__
# define M0_UT_ASSERT(a)	({ CU_ASSERT(a) })
# define M0_UT_PASS(m)		({ CU_PASS(m) })
# define M0_UT_FAIL(m)		({ CU_FAIL(m) })
#else
# define M0_UT_ASSERT(a)	m0_ut_assertimpl((a), __LINE__, #a, __FILE__)
# define M0_UT_PASS(m)		m0_ut_assertimpl(true, __LINE__, m, __FILE__)
# define M0_UT_FAIL(m)		m0_ut_assertimpl(false, __LINE__, m, __FILE__)
#endif

/**
   structure to define test in test suite.
 */
struct m0_test {
	/**
	   name of the test, must be unique.
	 */
	const char *t_name;
	/**
	   pointer to testing procedure
	 */
	void      (*t_proc)(void);
	/**
	   test's owner name
	 */
	const char *t_owner;
};

struct m0_test_suite {
	struct m0_list_link  ts_linkage;
	/**
	   name of a suite
	*/
	const char           *ts_name;
	/**
	   suite owners names
	*/
	const char           *ts_owners;
	/**
	   function to prepare tests in suite

	   @warning it's not allowed to use any of CUnit assertion macros, like
		    CU_ASSERT or M0_UT_ASSERT, in this function because it will
		    lead to a crash; use M0_ASSERT instead if required.
	 */
	int                 (*ts_init)(void);
	/**
	   function to free resources after tests run

	   @warning it's not allowed to use any of CUnit assertion macros, like
		    CU_ASSERT or M0_UT_ASSERT, in this function because it will
		    lead to a crash; use M0_ASSERT instead if required.
	 */
	int                 (*ts_fini)(void);
	/**
	   tests in suite
	 */
	const struct m0_test  ts_tests[];
};

struct m0_test_suite_entry {
	struct m0_list_link  tse_linkage;
	const char           *tse_suite_name;
	const char           *tse_test_name;
};

/**
   Global constructor for unit tests.
 */
int m0_ut_init(void);

/**
   Global destructor for unit tests.
 */
void m0_ut_fini(void);

/**
 add test site into global pool.
 if adding test suite failed application is aborted.

 @param ts pointer to test suite

 */
M0_INTERNAL void m0_ut_add(const struct m0_test_suite *ts);

/**
   CUnit user interfaces
 */
enum m0_ut_run_mode {
	M0_UT_KERNEL_MODE,    /** A stub for kernel version of m0_ut_run() */
	M0_UT_BASIC_MODE,     /** Basic CUnit interface with console output */
	M0_UT_ICONSOLE_MODE,  /** Interactive CUnit console interface */
	M0_UT_AUTOMATED_MODE, /** Automated CUnit interface with xml output */
};

/**
   Configuration parameters for m0_ut_run()
 */
struct m0_ut_run_cfg {
	/** CUnit interface mode */
	enum m0_ut_run_mode  urc_mode;
	/** if true, then set CUnit's assert mode to CUA_Abort */
	bool                 urc_abort_cu_assert;
	/** if true, then execution time is reported for each test */
	bool                 urc_report_exec_time;
	/**
	 * list of tests/suites to run, it can be empty, which means to run
	 * all the tests
	 */
	struct m0_list       *urc_test_list;
	/** list of tests/suites to exclude from running, it also can be empty */
	struct m0_list       *urc_exclude_list;
};

#ifndef __KERNEL__
/**
   run tests
 */
M0_INTERNAL void m0_ut_run(struct m0_ut_run_cfg *c);
#else
void m0_ut_run(void);
#endif

/**
 print all available test suites in YAML format to STDOUT

 @param with_tests - if true, then all tests of each suite are printed in
                     addition

 @return NONE
 */
M0_INTERNAL void m0_ut_list(bool with_tests);

/**
 * Print owners of all UTs on STDOUT
 */
M0_INTERNAL void m0_ut_owners_list(bool yaml);

/**
 commonly used test database reset function
 */
M0_INTERNAL int m0_ut_db_reset(const char *db_name);

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
M0_INTERNAL bool m0_ut_assertimpl(bool c, int lno, const char *str_c,
				  const char *file);
#endif

#ifndef __KERNEL__
struct m0_ut_redirect {
	FILE  *ur_stream;
	int    ur_oldfd;
	int    ur_fd;
	fpos_t ur_pos;
};

/**
 * Associates one of the standard streams (stdin, stdout, stderr) with a file
 * pointed by 'path' argument.
 */
M0_INTERNAL void m0_stream_redirect(FILE * stream, const char *path,
				    struct m0_ut_redirect *redir);

/**
 * Restores standard stream from file descriptor and stream position, which were
 * saved earlier by m0_stream_redirect().
 */
M0_INTERNAL void m0_stream_restore(const struct m0_ut_redirect *redir);

/**
 * Checks if a text file contains the specified string.
 *
 * @param fp   - a file, which is searched for a string
 * @param mesg - a string to search for
 */
M0_INTERNAL bool m0_error_mesg_match(FILE * fp, const char *mesg);
#endif

void m0_ut_fom_phase_set(struct m0_fom *fom, int phase);

/**
   @} ut end group
*/

#endif /* __MERO_UT_UT_H__ */
