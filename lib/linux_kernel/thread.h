/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 02/18/2011
 */

#pragma once

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

enum {
	C2_THREAD_NAME_LEN = TASK_COMM_LEN
};

int c2_thread_setspecific(const void *value);
void *c2_thread_getspecific(void);

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
