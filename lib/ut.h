#ifndef __COLIBRI_LIB_UT_H_

#define __COLIBRI_LIB_UT_H_

#include <CUnit/Basic.h>

/**
   @defgroup ut Unit testing.

   CUnit based unit testing support

   @{
 */

#define C2_UT_ASSERT(a)	CU_ASSERT(a)

/**
 structure to define test in test suite.
 */
struct c2_test {
	/**
	 name of test, must be unique.
	 */
	char *t_name;
	/**
	 pointer to testing procedure
	 */
	void (*t_proc)(void);
};

struct c2_test_suite {
	/**
	 name of a suite
	 */
	char *ts_name;
	/**
	 function to prepare tests in suet
	 */
	int (*ts_init)(void);
	/**
	 function to free resources after tests run
	 */
	int (*ts_fini)(void);
	/**
	 number tests in suet
	 */
	int ts_ntests;
	/**
	 tests in suite
	 */
	const struct c2_test *ts_tests;
};

/**
 constructor for a tests.
 initialize test site and specifies file name to write a log.
 if constructor failed - we not need call destructor to clear
 resources.
 
 @retval 0 if initialize finished without error
 @retval -errno if error is hit.
 */
int c2_ut_init(void);

/**
 destructor for the test suite
 need to be called after error in adding tests to suite
 of after run tests.
 
 @return status of last failed operation
*/
int c2_ut_fini(void);

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

/** @} end of ut group. */

#endif
