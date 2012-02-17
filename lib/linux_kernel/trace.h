/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Andriy Tkachuk <Andriy_Tkachuk@xyratex.com>
 * Original creation date: 02/17/2012
 */

#ifndef __COLIBRI_LIB_LINUX_KERNEL_TRACE_H__
#define __COLIBRI_LIB_LINUX_KERNEL_TRACE_H__

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/**
   @defgroup trace Tracing

   Kernel specific definitions.

 */

#ifdef ENABLE_DEBUG
#  define C2_TRACE_IMMEDIATE_DEBUG (1)
#else
#  define C2_TRACE_IMMEDIATE_DEBUG (0)
#endif

#define c2_printf printk

/** @} end of trace group */

/* __COLIBRI_LIB_LINUX_KERNEL_TRACE_H__ */
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
