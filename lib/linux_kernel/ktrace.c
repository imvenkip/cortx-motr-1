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
#include "lib/arith.h" /* m0_align */
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
		 " a bitmask or comma separated list of subsystem names"
		 " of what should be printed immediately to console");

static char *trace_level;
module_param(trace_level, charp, 0644);
MODULE_PARM_DESC(trace_level,
		 " trace level: level[+][,level[+]] where level is one of"
		 " call|debug|info|notice|warn|error|fatal");

static char *trace_print_context;
module_param(trace_print_context, charp, 0644);
MODULE_PARM_DESC(trace_print_context,
		 " controls whether to display additional trace point"
		 " info, like subsystem, file, func, etc.; values:"
		 " none, func, short, full");

static int set_trace_immediate_mask(void)
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
		m0_trace_immediate_mask = mask;
		goto out;
	}

	/*
	 * check if 'trace_immediate_mask' contains a comma-separated list of
	 * valid subsystem names
	 */
	mask_str = kstrdup(trace_immediate_mask, GFP_KERNEL);
	if (mask_str == NULL)
		return -ENOMEM;
	rc = m0_trace_subsys_list_to_mask(mask_str, &mask);
	kfree(mask_str);

	if (rc != 0)
		return rc;

	m0_trace_immediate_mask = mask;
out:
	pr_info("Mero trace immediate mask: 0x%lx\n",
			m0_trace_immediate_mask);

	return 0;
}

static int set_trace_level(void)
{
	char *level_str;

	/* check if argument was specified for 'trace_level' param */
	if (trace_level == NULL)
		return 0;

	level_str = kstrdup(trace_level, GFP_KERNEL);
	if (level_str == NULL)
		return -ENOMEM;

	m0_trace_level = m0_trace_parse_trace_level(level_str);
	kfree(level_str);

	if (m0_trace_level == M0_NONE)
		return -EINVAL;

	pr_info("Mero trace level: %s\n", trace_level);

	return 0;
}

static int set_trace_print_context(void)
{
	enum m0_trace_print_context ctx;

	/* check if argument was specified for 'trace_print_context' param */
	if (trace_print_context == NULL)
		return 0;

	ctx = m0_trace_parse_trace_print_context(trace_print_context);
	if (ctx == M0_TRACE_PCTX_INVALID)
		return -EINVAL;

	m0_trace_print_context = ctx;

	pr_info("Mero trace print context: %s\n", trace_print_context);

	return 0;
}

M0_INTERNAL int m0_arch_trace_init(void)
{
	int rc;

	rc = set_trace_immediate_mask();
	if (rc != 0)
		return rc;

	rc = set_trace_level();
	if (rc != 0)
		return rc;

	rc = set_trace_print_context();
	if (rc != 0)
		return rc;

	m0_logbuf = vmalloc(m0_logbufsize);
	if (m0_logbuf == NULL)
		return -ENOMEM;

	pr_info("Mero trace buffer address: 0x%p\n", m0_logbuf);

	return 0;
}

M0_INTERNAL void m0_arch_trace_fini(void)
{
	vfree(m0_logbuf);
}

M0_INTERNAL void m0_console_vprintf(const char *fmt, va_list args)
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
