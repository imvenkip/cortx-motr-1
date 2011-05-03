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
