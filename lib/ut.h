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
