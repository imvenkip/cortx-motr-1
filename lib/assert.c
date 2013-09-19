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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 05/06/2010
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_LIB
#include "lib/trace.h"

#include "lib/assert.h"
#include "lib/misc.h"      /* M0_CAT */
#include "mero/version.h"  /* m0_build_info */

/**
   @addtogroup assert

   @{
*/

/**
 * Panic function.
 */
void m0_panic(const char *expr, const char *func, const char *file, int lineno,
	      const char *fmt, ...)
{
	static int repanic = 0;
	va_list    ap;

	struct m0_panic_ctx c = {
		.pc_expr   = expr,
		.pc_func   = func,
		.pc_file   = file,
		.pc_lineno = lineno,
		.pc_bi     = m0_build_info_get(),
	};

	if (repanic++ == 0) {
		M0_LOG(M0_FATAL, "panic: %s at %s() (%s:%i) %s [git: %s]",
		       c.pc_expr, c.pc_func, c.pc_file, c.pc_lineno,
		       m0_failed_condition ?: "", c.pc_bi->bi_git_describe);
		va_start(ap, fmt);
		m0_arch_panic(&c, fmt, ap);
		va_end(ap);
	} else {
		/* The death of God left the angels in a strange position. */
		while (true) {
			;
		}
	}
}
M0_EXPORTED(m0_panic);

void m0_backtrace(void)
{
	m0_arch_backtrace();
}
M0_EXPORTED(m0_backtrace);

/** @} end of assert group */
#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
