/* -*- C -*- */

#ifndef __COLIBRI_LIB_ASSERT_H__
#define __COLIBRI_LIB_ASSERT_H__

#include <assert.h>

/**
   @defgroup assert Assertions, pre-conditions, post-conditions, invariants.
   @{
*/

#define C2_ASSERT(cond) assert(cond)
#define C2_PRE(cond) C2_ASSERT(cond)
#define C2_POST(cond) C2_ASSERT(cond)

#define C2_CASSERT(cond) extern char __c2_nowhere[!!(cond) - 1]

/** @} end of cc group */


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
