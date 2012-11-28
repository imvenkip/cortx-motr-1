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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 *		    Alexey Lyashkov <Alexey_Lyashkov@xyratex.com>
 * Original creation date: 04/07/2010
 */

#pragma once

#ifndef __MERO_LIB_CDEFS_H__
#define __MERO_LIB_CDEFS_H__

#ifndef __KERNEL__
#include "user_space/cdefs.h"
#else
#include "linux_kernel/cdefs.h"
#endif

/*
 * Helper macros for implication and equivalence.
 *
 * Unfortunately, name clashes are possible and m0_ prefix is too awkward. See
 * M0_BASSERT() checks in lib/misc.c
 */
#ifndef ergo
#define ergo(a, b) (!(a) || (b))
#endif

#ifndef equi
#define equi(a, b) (!(a) == !(b))
#endif

void __dummy_function(void);

/**
 * A macro used with if-statements without `else' clause to assure proper
 * coverage analysis.
 */
#define AND_NOTHING_ELSE else __dummy_function();

#define m0_is_array(x) \
	(!__builtin_types_compatible_p(typeof(&(x)[0]), typeof(x)))

#define IS_IN_ARRAY(idx, array)				\
({							\
	M0_CASSERT(m0_is_array(array));			\
							\
	((unsigned long)(idx)) < ARRAY_SIZE(array);	\
})

/**
 * Produces an expression having the same type as a given field in a given
 * struct or union. Suitable to be used as an argument to sizeof() or typeof().
 */
#define M0_FIELD_VALUE(type, field) (((type *)0)->field)

/**
 * True if an expression has a given type.
 */
#define M0_HAS_TYPE(expr, type) __builtin_types_compatible_p(typeof(expr), type)

/**
 * True iff type::field is of type "ftype".
 */
#define M0_FIELD_IS(type, field, ftype) \
	M0_HAS_TYPE(M0_FIELD_VALUE(type, field), ftype)

/**
 * Computes offset of "magix" field, iff magix field is of type uint64_t.
 * Otherwise causes compilation failure.
 */
#define M0_MAGIX_OFFSET(type, field)				\
M0_FIELD_IS(type, field, uint64_t) ?				\
	 offsetof(type, field) :				\
	 sizeof(char [M0_FIELD_IS(type, field, uint64_t) - 1])

/**
 * Returns the number of parameters given to this variadic macro (up to 9
 * parameters are supported)
 * @note M0_COUNT_PARAMS() returns max(number_of_parameters - 1, 0)
 *     e.g. M0_COUNT_PARAMS()        -> 0
 *          M0_COUNT_PARAMS(x)       -> 0
 *          M0_COUNT_PARAMS(x, y)    -> 1
 *          M0_COUNT_PARAMS(x, y, z) -> 2
 */
#define M0_COUNT_PARAMS(...) \
	M0_COUNT_PARAMS2(__VA_ARGS__, 9,8,7,6,5,4,3,2,1,0)
#define M0_COUNT_PARAMS2(_0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_, ...) _

/**
 * Concatenates two arguments to produce a single token.
 */
#define M0_CAT(A, B) M0_CAT2(A, B)
#define M0_CAT2(A, B) A ## B


/**
 * Check printf format string against parameters.
 *
 * This function does nothing except checking that the format string matches the
 * rest of arguments and producing a compilation warning in case it doesn't. It
 * is handy in macros which accept printf-like parameters with a format string.
 *
 * For example usage, refer to M0_TRACE_POINT() macro
 */
__attribute__ ((format (printf, 1, 2))) static inline void
printf_check(const char *fmt, ...)
{}


#define M0_UNUSED __attribute__((unused))

/* __MERO_LIB_CDEFS_H__ */
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
