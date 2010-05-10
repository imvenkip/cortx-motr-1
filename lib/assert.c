/* -*- C -*- */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>  /* fprintf, fflush */
#include <errno.h>
#include <stdlib.h> /* abort */

#ifdef HAVE_BACKTRACE
#  include <execinfo.h>
#endif

#include "lib/assert.h"
#include "lib/cdefs.h"

/**
   @addtogroup assert
   @{
*/

enum {
	BACKTRACE_DEPTH_MAX = 256
};

void c2_panic(const char *expr, const char *func, const char *file, int lineno)
{
	fprintf(stderr, "Assertion failure: %s at %s() %s:%i (errno: %i)\n",
		expr, func, file, lineno, errno);
	fflush(stderr);
#ifdef HAVE_BACKTRACE
	{
		void *trace[BACKTRACE_DEPTH_MAX];
		int nr;

		nr = backtrace(trace, ARRAY_SIZE(trace));
		backtrace_symbols_fd(trace, nr, 2);
	}
#endif
	abort();
}

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
