/* -*- C -*- */

#include <stdio.h>  /* fprintf, fflush */
#include <errno.h>
#include <stdlib.h> /* abort */

#include "assert.h"

/**
   @addtogroup assert
   @{
*/

int c2_panic(const char *expr, const char *file, int lineno)
{
	fprintf(stderr, "Assertion failure: %s at %s:%i (errno: %i)\n",
		expr, file, lineno, errno);
	fflush(stderr);
	abort();
	return 0;
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
