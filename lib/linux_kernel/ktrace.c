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
#include <linux/jiffies.h>           /* time_in_range_open */

#include "lib/errno.h"
#include "lib/atomic.h"
#include "lib/arith.h"  /* m0_align */
#include "lib/memory.h" /* m0_free */
#include "lib/string.h" /* m0_strdup */
#include "lib/trace.h"
#include "lib/trace_internal.h"
#include "lib/linux_kernel/trace.h"

/**
 * @addtogroup trace
 *
 * <b>Tracing facilities kernel specific stuff</b>
 *
 * @{
 */

static char *trace_immediate_mask;
module_param(trace_immediate_mask, charp, S_IRUGO);
MODULE_PARM_DESC(trace_immediate_mask,
		 " a bitmask or comma separated list of subsystem names"
		 " of what should be printed immediately to console");

static char *trace_level;
module_param(trace_level, charp, S_IRUGO);
MODULE_PARM_DESC(trace_level,
		 " trace level: level[+][,level[+]] where level is one of"
		 " call|debug|info|notice|warn|error|fatal");

static char *trace_print_context;
module_param(trace_print_context, charp, S_IRUGO);
MODULE_PARM_DESC(trace_print_context,
		 " controls whether to display additional trace point"
		 " info, like subsystem, file, func, etc.; values:"
		 " none, func, short, full");

static unsigned int trace_buf_size = M0_TRACE_BUFSIZE;
module_param(trace_buf_size, uint, S_IRUGO);
MODULE_PARM_DESC(trace_buf_size, "size of trace buffer in bytes");

static struct m0_trace_stats stats;

M0_INTERNAL int m0_trace_set_immediate_mask(const char *mask_str)
{
	int            rc;
	unsigned long  mask;
	char          *mask_str_copy;

	/* check if argument was specified for 'mask_str' param */
	if (mask_str == NULL)
		return 0;

	/* first, check if 'mask_str' contains a numeric bitmask */
	rc = strict_strtoul(mask_str, 0, &mask);
	if (rc == 0) {
		m0_trace_immediate_mask = mask;
		goto out;
	}

	/*
	 * if above strtoul() conversion has failed it means that mask_str
	 * contains a comma-separated list of subsystem names
	 */
	mask_str_copy = m0_strdup(mask_str);
	if (mask_str_copy == NULL)
		return -ENOMEM;
	rc = m0_trace_subsys_list_to_mask(mask_str_copy, &mask);
	m0_free(mask_str_copy);

	if (rc != 0)
		return rc;

	m0_trace_immediate_mask = mask;
out:
	pr_info("Mero trace immediate mask: 0x%lx\n", m0_trace_immediate_mask);
	return 0;
}
M0_EXPORTED(m0_trace_set_immediate_mask);

M0_INTERNAL const struct m0_trace_stats *m0_trace_get_stats(void)
{
	return &stats;
}
M0_EXPORTED(m0_trace_get_stats);

M0_INTERNAL void m0_trace_update_stats(uint32_t rec_size)
{
	static unsigned long prev_jiffies = INITIAL_JIFFIES;
	static uint64_t      prev_logbuf_pos;
	static uint64_t      prev_rec_total;

	if (prev_jiffies == INITIAL_JIFFIES) {
		prev_jiffies = jiffies;
		prev_logbuf_pos = 0;
		prev_rec_total = 0;
	}

	if (rec_size > 0) {
		m0_atomic64_inc(&stats.trs_rec_total);
		stats.trs_avg_rec_size = (rec_size + stats.trs_avg_rec_size) / 2;
		stats.trs_max_rec_size = max(rec_size, stats.trs_max_rec_size);
	}

	if (time_after_eq(jiffies, prev_jiffies + HZ)) {
		stats.trs_rec_per_sec =
			m0_atomic64_get(&stats.trs_rec_total) - prev_rec_total;
		stats.trs_bytes_per_sec =
			m0_trace_get_logbuf_pos() - prev_logbuf_pos;

		stats.trs_avg_rec_per_sec
			= (stats.trs_rec_per_sec + stats.trs_avg_rec_per_sec) / 2;
		stats.trs_avg_bytes_per_sec
			= (stats.trs_bytes_per_sec + stats.trs_avg_bytes_per_sec) / 2;

		stats.trs_max_rec_per_sec
			= max(stats.trs_rec_per_sec, stats.trs_max_rec_per_sec);
		stats.trs_max_bytes_per_sec
			= max(stats.trs_bytes_per_sec, stats.trs_max_bytes_per_sec);

		prev_jiffies = jiffies;
		prev_logbuf_pos = m0_trace_get_logbuf_pos();
		prev_rec_total = m0_atomic64_get(&stats.trs_rec_total);
	}
}
M0_EXPORTED(m0_trace_update_stats);

M0_INTERNAL void m0_console_vprintf(const char *fmt, va_list args)
{
	vprintk(fmt, args);
}

M0_INTERNAL int m0_arch_trace_init(uint32_t default_logbuf_size)
{
	int rc;

	m0_atomic64_set(&stats.trs_rec_total, 0);

	rc = m0_trace_set_immediate_mask(trace_immediate_mask);
	if (rc != 0)
		return rc;

	rc = m0_trace_set_level(trace_level);
	if (rc != 0)
		return rc;

	rc = m0_trace_set_print_context(trace_print_context);
	if (rc != 0)
		return rc;

	if (trace_buf_size == 0 || !m0_is_po2(trace_buf_size)) {
		pr_err("mero: incorrect value for trace_buffer_size parameter,"
		       " it can't be zero and should be a power of 2 value\n");
		return -EINVAL;
	}

	m0_logbuf = vmalloc(trace_buf_size);
	if (m0_logbuf == NULL) {
		pr_err("mero: failed to allocate %u bytes for trace buffer\n",
		       trace_buf_size);
		return -ENOMEM;
	}
	m0_logbufsize = trace_buf_size;

	pr_info("mero: trace buffer address: 0x%p\n", m0_logbuf);

	return 0;
}

M0_INTERNAL void m0_arch_trace_fini(void)
{
	vfree(m0_logbuf);
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
