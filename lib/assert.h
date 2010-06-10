/* -*- C -*- */

#ifndef __COLIBRI_LIB_ASSERT_H__
#define __COLIBRI_LIB_ASSERT_H__

/**
   @defgroup assert Assertions, pre-conditions, post-conditions, invariants.

   This module provides:

   @li C2_ASSERT(condition): a macro to assert that condition is true. If
   condition is true, C2_ASSERT() does nothing. Otherwise it emits a diagnostics
   message and terminates the system. The message and the termination method are
   platform dependent.

   @li C2_PRE(precondition): a macro to check that a function pre-condition
   holds. C2_PRE() is functionally equivalent to C2_ASSERT().

   @li C2_POST(postcondition): a macro to check that a function post-condition
   holds. C2_POST() is functionally equivalent to C2_ASSERT().

   @li C2_CASSERT(condition): a macro to assert that compile-time condition is
   true. Condition must be a constant expression (as defined in the section 6.6
   of ISO/IEC 9899). C2_CASSERT() can be used anywhere where a statement can be.

   @li C2_IMPOSSIBLE(message): a macro indicating that computation reached
   invalid state.

   @todo additional form of compile-time assertion checking macro for use in
   header files.

   @{
*/

void c2_panic(const char *expr, const char *func, const char *file, int lineno) 
	__attribute__((noreturn));

#define C2_ASSERT(cond) \
        ((cond) ? (void)0 : c2_panic(#cond, __func__, __FILE__, __LINE__))

#define C2_PRE(cond) C2_ASSERT(cond)
#define C2_POST(cond) C2_ASSERT(cond)

#define C2_CASSERT(cond) do { switch (1) {case 0: case !!(cond): ;} } while (0)

#define C2_IMPOSSIBLE(msg) \
        c2_panic("Impossible: " msg, __func__, __FILE__, __LINE__)

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
