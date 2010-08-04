/* -*- C -*- */

#ifndef __COLIBRI_LIB_LINUX_KERNEL_ASSERT_H__
#define __COLIBRI_LIB_LINUX_KERNEL_ASSERT_H__

/**
   @addtogroup assert 

   <b>Linux kernel assertion mechanism.</b>

   Based on standard BUG_ON() macro.

   @{
*/

#define C2_ASSERT(cond) BUG_ON(!(cond))
#define C2_PRE(cond) C2_ASSERT(cond)
#define C2_POST(cond) C2_ASSERT(cond)
#define C2_CASSERT(cond) do { switch (1) {case 0: case !!(cond): ;} } while (0)
#define C2_BASSERT(cond) extern char __build_assertion_failure[!!(cond) - 1]
#define C2_IMPOSSIBLE(msg) C2_ASSERT(msg == NULL)

/** @} end of assert group */

/* __COLIBRI_LIB_LINUX_KERNEL_ASSERT_H__ */
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
