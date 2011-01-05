/* -*- C -*- */

#ifndef __COLIBRI_LIB_CDEFS_H__
#define __COLIBRI_LIB_CDEFS_H__

#ifndef __KERNEL__
#include "user_space/cdefs.h"
#else
#include "linux_kernel/cdefs.h"
#endif

/*
 * Helper macros for implication and equivalence.
 *
 * Unfortunately, name clashes are possible and c2_ prefix is too awkward. See
 * C2_BASSERT() checks in lib/misc.c
 */

#ifndef ergo
#define ergo(a, b) (!(a) || (b))
#endif

#ifndef equi
#define equi(a, b) (!(a) == !(b))
#endif

extern void __dummy_function(void);

/**
   A macro used with if-statements without `else' clause to assure proper
   coverage analysis.
 */
#define AND_NOTHING_ELSE else __dummy_function();

#define c2_is_array(x) \
	(!__builtin_types_compatible_p(typeof(&(x)[0]), typeof(x)))

#define IS_IN_ARRAY(idx, array)				\
({							\
	C2_CASSERT(c2_is_array(array));			\
							\
	((unsigned long)(idx)) < ARRAY_SIZE(array);	\
})

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
