/* -*- C -*- */

#ifndef __COLIBRI_LIB_LINUX_KERNEL_SEMAPHORE_H__
#define __COLIBRI_LIB_LINUX_KERNEL_SEMAPHORE_H__

#include <linux/semaphore.h>

/**
   @addtogroup semaphore

   <b>Linux kernel semaphore.</a>
   @{
 */

struct c2_semaphore {
	struct semaphore s_sem;
};

/** @} end of semaphore group */

/* __COLIBRI_LIB_LINUX_KERNEL_SEMAPHORE_H__ */
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
