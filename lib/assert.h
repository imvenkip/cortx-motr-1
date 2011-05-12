/* -*- C -*- */

#ifndef __COLIBRI_LIB_ASSERT_H__
#define __COLIBRI_LIB_ASSERT_H__

/**
   @defgroup assert Assertions, pre-conditions, post-conditions, invariants.

   @{
*/

#ifndef __KERNEL__
#include "user_space/assert.h"
#else
#include "linux_kernel/assert.h"
#endif

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
#define C2_BASSERT(cond) extern int __dummy_[sizeof(struct { \
	int __build_time_assertion_failure:!!(cond); })]

/**
   A macro indicating that computation reached an invalid state.
 */
#define C2_IMPOSSIBLE(msg) C2_ASSERT("Impossible: " msg == NULL)

/** @} end of assert group */

/* __COLIBRI_LIB_ASSERT_H__ */
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
