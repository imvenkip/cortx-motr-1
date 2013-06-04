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
#include "lib/cdefs.h"

/**
   @addtogroup assert

   @{
*/

/**
 * Panic function.
 */
void m0_panic(const char *expr, const char *func, const char *file, int lineno)
{
	static int repanic = 0;

	if (repanic++ == 0) {
		M0_LOG(M0_FATAL, "panic: %s %s() (%s:%i) %s",
		       expr, func, file, lineno,
		       m0_failed_condition ?: "");
		m0_arch_panic(expr, func, file, lineno);
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

#undef M0_TRACE_SUBSYSTEM

/** @} end of assert group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
