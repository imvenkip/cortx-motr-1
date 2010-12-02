/* -*- C -*- */

#ifndef __COLIBRI_LIB_LINUX_KERNEL_RWLOCK_H__
#define __COLIBRI_LIB_LINUX_KERNEL_RWLOCK_H__

#include <linux/mutex.h>

/**
   @addtogroup rwlock

   <b>Linux kernel rwlock.</a>
   @{
 */

struct c2_rwlock {
	rwlock_t m_rwlock;
};

/** @} end of mutex group */

/* __COLIBRI_LIB_LINUX_KERNEL_RWLOCK_H__ */
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
