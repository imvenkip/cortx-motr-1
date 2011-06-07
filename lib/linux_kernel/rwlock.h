/* -*- C -*- */

#ifndef __COLIBRI_LIB_LINUX_KERNEL_RWLOCK_H__
#define __COLIBRI_LIB_LINUX_KERNEL_RWLOCK_H__

#include <linux/rwsem.h>

/**
   @addtogroup rwlock

   <b>Linux kernel rwlock.</a>

   Linux kernel implementation is based on rw_semaphore (linux/rwsem.h).

   @{
 */

struct c2_rwlock {
	struct rw_semaphore rw_sem;
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
