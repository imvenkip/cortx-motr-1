/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 31-Jan-2013
 */


/**
 * @addtogroup assert
 *
 * @{
 */

#include <stdio.h>  /* fprintf, fflush */
#include <stdlib.h> /* abort */
#include <unistd.h> /* fork, execvp */

#ifdef HAVE_BACKTRACE
#  include <execinfo.h>
#endif

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_LIB
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/cdefs.h"

/**
   @addtogroup assert

   User space m0_arch_panic() implementation.
   @{
*/

enum {
	BACKTRACE_DEPTH_MAX = 256
};

M0_EXTERN char *m0_debugger_args[4];

/**
   Simple user space panic function: issue diagnostics to the stderr, flush the
   stream, optionally print the backtrace and abort(3) the program.

   Stack back-trace printing uses GNU extensions to the libc, declared in
   <execinfo.h> header (checked for by ./configure). Object files should be
   compiled with -rdynamic for this to work in the presence of dynamic linking.
 */
void m0_arch_panic(const char *expr, const char *func,
		   const char *file, int lineno)
{
	fprintf(stderr,
		"Mero panic: %s at %s() %s:%i (errno: %i) (last failed: %s)\n",
		expr, func, file, lineno, errno,
		m0_failed_condition ?: "none");
	fflush(stderr);
#ifdef HAVE_BACKTRACE
	{
		void *trace[BACKTRACE_DEPTH_MAX];
		int nr;

		nr = backtrace(trace, ARRAY_SIZE(trace));
		backtrace_symbols_fd(trace, nr, 2);
	}
#endif
	if (m0_debugger_args[0] != NULL) {
		int rc;

		rc = fork();
		if (rc > 0) {
			/* parent */
			volatile bool stop = true;

			while (stop) {
				;
			}
		} else if (rc == 0) {
			/* child */
			rc = execvp(m0_debugger_args[0], m0_debugger_args);
		}
	}
	abort();
}

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
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
