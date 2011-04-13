#ifndef __COLIBRI_LIB_UT_H_
#define __COLIBRI_LIB_UT_H_

#ifndef __KERNEL__
# include <CUnit/Basic.h>
#endif

/**
   @defgroup ut Unit testing.

   CUnit based unit testing support

   @{
 */

#ifndef __KERNEL__
# define C2_UT_ASSERT(a)	CU_ASSERT(a)
#else
# define C2_UT_ASSERT(a)	__C2_UT_ASSERTIMPL((a), __LINE__, #a, __FILE__)
# define __C2_UT_ASSERTIMPL(c, l, s, f)					      \
({									      \
	bool __r = (c);						              \
	if (!__r)							      \
		printk(KERN_INFO					      \
		       "Unit test assertion failed: %s at %s:%d\n", s, f, l); \
	__r;								      \
})
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
