/* -*- C -*- */

#ifndef __COLIBRI_LIB_LINUX_KERNEL_TIME_H__
#define __COLIBRI_LIB_LINUX_KERNEL_TIME_H__

#include <linux/time.h>

/**
   @addtogroup time

   <b>Linux kernel time.</a>
   @{
 */

/* kernel and userspace both happen to have a struct timespec, but defined in
 different headers */
struct c2_time {
	struct timespec ts;
};

/** @} end of time group */

/* __COLIBRI_LIB_LINUX_KERNEL_TIME_H__ */
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
