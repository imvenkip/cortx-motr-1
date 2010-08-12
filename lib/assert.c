/* -*- C -*- */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>  /* fprintf, fflush */
#include <stdlib.h> /* abort */

#ifdef HAVE_BACKTRACE
#  include <execinfo.h>
#endif

#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/cdefs.h"

/**
   @addtogroup assert

   User space c2_panic() implementation.
   @{
*/

enum {
	BACKTRACE_DEPTH_MAX = 256
};

/**
   Simple user space panic function: issue diagnostics to the stderr, flush the
   stream, optionally print the backtrace and abort(3) the program.

   Stack back-trace printing uses GNU extensions to the libc, declared in
   <execinfo.h> header (checked for by ./configure). Object files should be
   compiled with -rdynamic for this to work in the presence of dynamic linking.
 */
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
	C2_TRACE_POINT( { /* emit a trace point for panic */
			const char *expr; 
			const char *func;
			const char *file;
			int         lineno; }, expr, func, file, lineno);
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
