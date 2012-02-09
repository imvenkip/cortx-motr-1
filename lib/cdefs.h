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
 *		    Alexey Lyashkov <Alexey_Lyashkov@xyratex.com>
 * Original creation date: 04/07/2010
 */

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
 * A macro used with if-statements without `else' clause to assure proper
 * coverage analysis.
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

/**
 * Produces an expression having the same type as a given field in a given
 * struct or union. Suitable to be used as an argument to sizeof() or typeof().
 */
#define C2_FIELD_VALUE(type, field) (((type *)0)->field)

/**
 * True if an expression has a given type.
 */
#define C2_HAS_TYPE(expr, type) __builtin_types_compatible_p(typeof(expr), type)

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
