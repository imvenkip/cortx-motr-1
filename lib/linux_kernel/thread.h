/* -*- C -*- */

#ifndef __COLIBRI_LIB_LINUX_KERNEL_THREAD_H__
#define __COLIBRI_LIB_LINUX_KERNEL_THREAD_H__

#include <linux/kthread.h>

/**
   @addtogroup thread Thread

   <b>Linux kernel c2_thread implementation</b>

   Kernel space implementation is based <linux/kthread.h>

   @see c2_thread

   @{
 */

struct c2_thread_handle {
	struct task_struct *h_t;
};

/** @} end of thread group */

/* __COLIBRI_LIB_LINUX_KERNEL_THREAD_H__ */
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
