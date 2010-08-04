/* -*- C -*- */

#ifndef __COLIBRI_LIB_USER_SPACE_ASSERT_H__
#define __COLIBRI_LIB_USER_SPACE_ASSERT_H__

/**
   @addtogroup assert 

   <b>User space assertions based on c2_panic() function.</b>

   @{
*/

void c2_panic(const char *expr, const char *func, const char *file, int lineno) 
	__attribute__((noreturn));

/**
   A macro to assert that a condition is true. If condition is true, C2_ASSERT()
   does nothing. Otherwise it emits a diagnostics message and terminates the
   system. The message and the termination method are platform dependent.
 */
#define C2_ASSERT(cond) \
        ((cond) ? (void)0 : c2_panic(#cond, __func__, __FILE__, __LINE__))

/**
   A macro to check that a function pre-condition holds. C2_PRE() is
   functionally equivalent to C2_ASSERT().

   @see C2_POST()
 */
#define C2_PRE(cond) C2_ASSERT(cond)

/**
   A macro to check that a function post-condition holds. C2_POST() is
   functionally equivalent to C2_ASSERT().

   @see C2_PRE()
 */
#define C2_POST(cond) C2_ASSERT(cond)

/**
   A macro to assert that compile-time condition is true. Condition must be a
   constant expression (as defined in the section 6.6 of ISO/IEC
   9899). C2_CASSERT() can be used anywhere where a statement can be.

   @see C2_BASSERT()
 */
#define C2_CASSERT(cond) do { switch (1) {case 0: case !!(cond): ;} } while (0)

/**
   A macro to assert that compile-time condition is true. Condition must be a
   constant expression. C2_BASSERT() can be used anywhere where a declaration
   can be.

   @see C2_CASSERT()
 */
#define C2_BASSERT(cond) extern char __build_assertion_failure[!!(cond) - 1]

/**
   A macro indicating that computation reached an invalid state.
 */
#define C2_IMPOSSIBLE(msg) C2_ASSERT("Impossible: " msg == NULL)

/** @} end of assert group */

/* __COLIBRI_LIB_USER_SPACE_ASSERT_H__ */
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
