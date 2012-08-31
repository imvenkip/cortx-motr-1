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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 06/18/2010
 */

#pragma once

#ifndef __COLIBRI_LIB_MISC_H__
#define __COLIBRI_LIB_MISC_H__

#ifndef __KERNEL__
#include <string.h>               /* memset, ffs */
#else
#include <linux/string.h>         /* memset */
#include <linux/bitops.h>         /* ffs */
#endif

#include "lib/types.h"
#include "lib/assert.h"           /* C2_CASSERT */
#include "lib/cdefs.h"            /* c2_is_array */
#include "lib/types.h"

/**
 * Returns rounded up value of @val in chunks of @size.
 * @pre c2_is_po2(size)
 */
uint64_t c2_round_up(uint64_t val, uint64_t size);

/**
 * Returns rounded down value of @val in chunks of @size.
 * @pre c2_is_po2(size)
 */
uint64_t c2_round_down(uint64_t val, uint64_t size);

#define C2_SET0(obj)				\
({						\
	C2_CASSERT(!c2_is_array(obj));		\
	memset((obj), 0, sizeof *(obj));	\
})

#define C2_SET_ARR0(arr)			\
({						\
	C2_CASSERT(c2_is_array(arr));		\
	memset((arr), 0, sizeof (arr));		\
})

/**
 * Returns a conjunction (logical AND) of an expression evaluated over a range
 *
 * Declares an unsigned integer variable named "var" in a new scope and
 * evaluates user-supplied expression (the last argument) with "var" iterated
 * over successive elements of [0 .. NR - 1] range, while this expression
 * returns true. Returns true iff the whole range was iterated over.
 *
 * This function is useful for invariant checking.
 *
 * @code
 * bool foo_invariant( struct foo *f)
 * {
 *        return c2_forall(i, ARRAY_SIZE(f->f_nr_bar), f->f_bar[i].b_count > 0);
 * }
 * @endcode
 *
 * @see c2_tlist_forall(), c2_tl_forall(), c2_list_forall().
 * @see c2_list_entry_forall().
 */
#define c2_forall(var, nr, ...)					\
({								\
	unsigned __nr = (nr);					\
	unsigned var;						\
								\
	for (var = 0; var < __nr && ({ __VA_ARGS__ ; }); ++var)	\
		;						\
	var == __nr;						\
})

/**
   Evaluates to true iff x is present in set.

   e.g. C2_IN(session->s_state, (C2_RPC_SESSION_IDLE,
                                 C2_RPC_SESSION_BUSY,
                                 C2_RPC_SESSION_TERMINATING))

   Parentheses around "set" members are mandatory.
 */
#define C2_IN(x, set) C2_IN0(x, C2_UNPACK set)
#define C2_UNPACK(...) __VA_ARGS__

#define C2_IN0(...) \
	C2_CAT(C2_IN_, C2_COUNT_PARAMS(__VA_ARGS__))(__VA_ARGS__)

#define C2_IN_1(x, v) ((x) == (v))
#define C2_IN_2(x, v, ...) ((x) == (v) || C2_IN_1(x, __VA_ARGS__))
#define C2_IN_3(x, v, ...) ((x) == (v) || C2_IN_2(x, __VA_ARGS__))
#define C2_IN_4(x, v, ...) ((x) == (v) || C2_IN_3(x, __VA_ARGS__))
#define C2_IN_5(x, v, ...) ((x) == (v) || C2_IN_4(x, __VA_ARGS__))
#define C2_IN_6(x, v, ...) ((x) == (v) || C2_IN_5(x, __VA_ARGS__))
#define C2_IN_7(x, v, ...) ((x) == (v) || C2_IN_6(x, __VA_ARGS__))
#define C2_IN_8(x, v, ...) ((x) == (v) || C2_IN_7(x, __VA_ARGS__))
#define C2_IN_9(x, v, ...) ((x) == (v) || C2_IN_8(x, __VA_ARGS__))

/**
   C2_BITS(...) returns bitmask of passed bits.
   e.g.
@code
   enum foo_states {
        FOO_UNINITIALISED,
        FOO_INITIALISED,
        FOO_ACTIVE,
        FOO_FAILED,
        FOO_NR,
   };
@endcode

   then @code C2_BITS(FOO_ACTIVE, FOO_FAILED) @endcode returns
   (1 << FOO_ACTIVE) | (1 << FOO_FAILED)

   @code C2_BITS() @endcode (C2_BITS) macro with no parameters will cause
   compilation failure.
*/
#define C2_BITS(...) \
C2_CAT(__C2_BITS_, C2_COUNT_PARAMS(__VA_ARGS__))(__VA_ARGS__)
#define __C2_BITS_0(i)(1 << (i))
#define __C2_BITS_1(i, ...) ((1 << (i)) | __C2_BITS_0(__VA_ARGS__))
#define __C2_BITS_2(i, ...) ((1 << (i)) | __C2_BITS_1(__VA_ARGS__))
#define __C2_BITS_3(i, ...) ((1 << (i)) | __C2_BITS_2(__VA_ARGS__))
#define __C2_BITS_4(i, ...) ((1 << (i)) | __C2_BITS_3(__VA_ARGS__))
#define __C2_BITS_5(i, ...) ((1 << (i)) | __C2_BITS_4(__VA_ARGS__))
#define __C2_BITS_6(i, ...) ((1 << (i)) | __C2_BITS_5(__VA_ARGS__))
#define __C2_BITS_7(i, ...) ((1 << (i)) | __C2_BITS_6(__VA_ARGS__))
#define __C2_BITS_8(i, ...) ((1 << (i)) | __C2_BITS_7(__VA_ARGS__))

const char *c2_bool_to_str(bool b);

/* __COLIBRI_LIB_MISC_H__ */
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
