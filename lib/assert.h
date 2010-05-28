/* -*- C -*- */

#ifndef __COLIBRI_LIB_ASSERT_H__
#define __COLIBRI_LIB_ASSERT_H__

/**
   @defgroup assert Assertions, pre-conditions, post-conditions, invariants.
   @{
*/

void c2_panic(const char *expr, const char *func, const char *file, int lineno) 
	__attribute__((noreturn));

#define C2_ASSERT(cond) \
        ((cond) ? (void)0 : c2_panic(#cond, __func__, __FILE__, __LINE__))

#define C2_PRE(cond) C2_ASSERT(cond)
#define C2_POST(cond) C2_ASSERT(cond)

#define C2_CASSERT(cond) do { switch (1) {case 0: case !!(cond): ;} } while (0)

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
