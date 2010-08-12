/* -*- C -*- */

#ifndef __COLIBRI_LIB_LINUX_KERNEL_MUTEX_H__
#define __COLIBRI_LIB_LINUX_KERNEL_MUTEX_H__

#include <linux/mutex.h>

/**
   @addtogroup mutex

   <b>Linux kernel mutex.</a>
   @{
 */

struct c2_mutex {
	struct mutex m_mutex;
};

/** @} end of mutex group */

/* __COLIBRI_LIB_LINUX_KERNEL_MUTEX_H__ */
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
