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

#ifndef __COLIBRI_LIB_UT_H_
#define __COLIBRI_LIB_UT_H_

#ifndef __KERNEL__
# include <CUnit/Basic.h>
#else
# include "lib/types.h"
#endif

/**
   @defgroup ut Unit testing.

   CUnit based unit testing support

   @{
 */

#ifndef __KERNEL__
# define C2_UT_ASSERT(a)	CU_ASSERT(a)
# define C2_UT_PASS(m)		CU_PASS(m)
# define C2_UT_FAIL(m)		CU_FAIL(m)
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
	 */
	int                 (*ts_init)(void);
	/**
	   function to free resources after tests run
	 */
	int                 (*ts_fini)(void);
	/**
	   tests in suite
	 */
	const struct c2_test  ts_tests[];
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
 run tests and write log into file

 @param log_file - name of file to a write testing log

 @return NONE
 */
void c2_ut_run(const char *log_file);

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

/** @} end of ut group. */

/* __COLIBRI_LIB_UT_H_ */



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
