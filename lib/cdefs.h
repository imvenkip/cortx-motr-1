/* -*- C -*- */

#ifndef __COLIBRI_LIB_CDEFS_H__
#define __COLIBRI_LIB_CDEFS_H__

#ifndef __KERNEL__
#include "user_space/cdefs.h"
#else
#include "linux_kernel/cdefs.h"
#endif

#define c2_ergo(a, b) (!(a) || (b))
#define c2_equi(a, b) (!(a) == !(b))

extern void __dummy_function(void);

/**
   A macro used with if-statements without `else' clause to assure proper
   coverage analysis.
 */
#define AND_NOTHING_ELSE else __dummy_function();

#define c2_is_array(x) \
	(!__builtin_types_compatible_p(typeof(&(x)[0]), typeof(x)))

/* __COLIBRI_LIB_CDEFS_H__ */
#endif

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
