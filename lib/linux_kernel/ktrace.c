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
 * Original author: Andriy Tkachuk <Andriy_Tkachuk@xyratex.com>
 * Original creation date: 02/16/2012
 */

#include <linux/slab.h>
#include <linux/sched.h>

#include "lib/errno.h"
#include "lib/atomic.h"
#include "lib/arith.h" /* c2_align */
#include "lib/trace.h"

/**
 * @addtogroup trace
 *
 * <b>Tracing facilities kernel specific stuff</b>
 *
 * @{
 */

extern void *c2_logbuf;

int c2_arch_trace_init(void)
{
	c2_logbuf = kzalloc(C2_TRACE_BUFSIZE, GFP_KERNEL);
	if (c2_logbuf == NULL)
		return -ENOMEM;

	printk("Colibri: trace buffer address: 0x%p\n", c2_logbuf);

	return 0;
}

void c2_arch_trace_fini(void)
{
	kfree(c2_logbuf);
}

/** @} end of trace group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
