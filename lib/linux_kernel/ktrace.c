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

#include <linux/vmalloc.h>           /* vmalloc, vfree */
#include <linux/kernel.h>            /* vprintk */

#include "lib/errno.h"
#include "lib/atomic.h"
#include "lib/arith.h" /* c2_align */
#include "lib/trace.h"
#include "lib/trace_internal.h"

/**
 * @addtogroup trace
 *
 * <b>Tracing facilities kernel specific stuff</b>
 *
 * @{
 */

static char *trace_immediate_mask;
module_param(trace_immediate_mask, charp, 0644);
MODULE_PARM_DESC(trace_immediate_mask,
		 "A bitmask or comma separated list of subsystem names"
		 " of what should be printed immediately to console");

static int parse_trace_immediate_mask(void)
{
	int            rc;
	char          *mask_str;
	unsigned long  mask;

	/* check if argument was specified for 'trace_immediate_mask' param */
	if (trace_immediate_mask == NULL)
		return 0;

	/* first, check if 'trace_immediate_mask' contains a numeric bitmask */
	rc = strict_strtoul(trace_immediate_mask, 0, &mask);
	if (rc == 0) {
		c2_trace_immediate_mask = mask;
		goto out;
	}

	/*
	 * check if 'trace_immediate_mask' contains a comma-separated list of
	 * valid subsystem names
	 */
	mask_str = kstrdup(trace_immediate_mask, GFP_KERNEL);
	if (mask_str == NULL)
		return -ENOMEM;
	rc = subsys_list_to_mask(mask_str, &mask);
	kfree(mask_str);

	if (rc != 0)
		return rc;

	c2_trace_immediate_mask = mask;
out:
	pr_info("Colibri trace immediate mask: 0x%lx\n",
			c2_trace_immediate_mask);

	return 0;
}

int c2_arch_trace_init(void)
{
	int rc;

	rc = parse_trace_immediate_mask();
	if (rc != 0)
		return rc;

	c2_logbuf = vmalloc(c2_logbufsize);
	if (c2_logbuf == NULL)
		return -ENOMEM;

	pr_info("Colibri trace buffer address: 0x%p\n", c2_logbuf);

	return 0;
}

void c2_arch_trace_fini(void)
{
	vfree(c2_logbuf);
}

void c2_console_vprintf(const char *fmt, va_list args)
{
	vprintk(fmt, args);
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
