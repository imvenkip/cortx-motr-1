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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 06/18/2010
 */

#pragma once

#ifndef __MERO_LIB_MISC_H__
#define __MERO_LIB_MISC_H__

#ifndef __KERNEL__
#include <string.h>               /* memset, ffs, strstr */
#else
#include <linux/string.h>         /* memset, strstr */
#include <linux/bitops.h>         /* ffs */
#endif

#include "lib/types.h"
#include "lib/assert.h"           /* M0_CASSERT */
#include "lib/cdefs.h"            /* m0_is_array */

/**
 * Returns rounded up value of @val in chunks of @size.
 * @pre m0_is_po2(size)
 */
M0_INTERNAL uint64_t m0_round_up(uint64_t val, uint64_t size);

/**
 * Returns rounded down value of @val in chunks of @size.
 * @pre m0_is_po2(size)
 */
M0_INTERNAL uint64_t m0_round_down(uint64_t val, uint64_t size);

#define M0_MEMBER_SIZE(type, member) sizeof(((type *)0)->member)

#define M0_SET0(obj)				\
({						\
	M0_CASSERT(!m0_is_array(obj));		\
	memset((obj), 0, sizeof *(obj));	\
})

#define M0_SET_ARR0(arr)			\
({						\
	M0_CASSERT(m0_is_array(arr));		\
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
 * bool foo_invariant(const struct foo *f)
 * {
 *        return m0_forall(i, ARRAY_SIZE(f->f_nr_bar), f->f_bar[i].b_count > 0);
 * }
 * @endcode
 *
 * @see m0_tlist_forall(), m0_tl_forall(), m0_list_forall().
 * @see m0_list_entry_forall().
 */
#define m0_forall(var, nr, ...)					\
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

   e.g. M0_IN(session->s_state, (M0_RPC_SESSION_IDLE,
                                 M0_RPC_SESSION_BUSY,
                                 M0_RPC_SESSION_TERMINATING))

   Parentheses around "set" members are mandatory.
 */
#define M0_IN(x, set)						\
	({ typeof (x) __x = (x);				\
		M0_IN0(__x, M0_UNPACK set); })

#define M0_UNPACK(...) __VA_ARGS__

#define M0_IN0(...) \
	M0_CAT(M0_IN_, M0_COUNT_PARAMS(__VA_ARGS__))(__VA_ARGS__)

#define M0_IN_1(x, v) ((x) == (v))
#define M0_IN_2(x, v, ...) ((x) == (v) || M0_IN_1(x, __VA_ARGS__))
#define M0_IN_3(x, v, ...) ((x) == (v) || M0_IN_2(x, __VA_ARGS__))
#define M0_IN_4(x, v, ...) ((x) == (v) || M0_IN_3(x, __VA_ARGS__))
#define M0_IN_5(x, v, ...) ((x) == (v) || M0_IN_4(x, __VA_ARGS__))
#define M0_IN_6(x, v, ...) ((x) == (v) || M0_IN_5(x, __VA_ARGS__))
#define M0_IN_7(x, v, ...) ((x) == (v) || M0_IN_6(x, __VA_ARGS__))
#define M0_IN_8(x, v, ...) ((x) == (v) || M0_IN_7(x, __VA_ARGS__))
#define M0_IN_9(x, v, ...) ((x) == (v) || M0_IN_8(x, __VA_ARGS__))

/**
   M0_BITS(...) returns bitmask of passed states.
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

   then @code M0_BITS(FOO_ACTIVE, FOO_FAILED) @endcode returns
   (1 << FOO_ACTIVE) | (1 << FOO_FAILED)

   @code M0_BITS() @endcode (M0_BITS macro with no parameters will cause
   compilation failure.
 */
#define M0_BITS(...) \
	M0_CAT(__M0_BITS_, M0_COUNT_PARAMS(__VA_ARGS__))(__VA_ARGS__)

#define __M0_BITS_0(i)        (1ULL << (i))
#define __M0_BITS_1(i, ...)  ((1ULL << (i)) | __M0_BITS_0(__VA_ARGS__))
#define __M0_BITS_2(i, ...)  ((1ULL << (i)) | __M0_BITS_1(__VA_ARGS__))
#define __M0_BITS_3(i, ...)  ((1ULL << (i)) | __M0_BITS_2(__VA_ARGS__))
#define __M0_BITS_4(i, ...)  ((1ULL << (i)) | __M0_BITS_3(__VA_ARGS__))
#define __M0_BITS_5(i, ...)  ((1ULL << (i)) | __M0_BITS_4(__VA_ARGS__))
#define __M0_BITS_6(i, ...)  ((1ULL << (i)) | __M0_BITS_5(__VA_ARGS__))
#define __M0_BITS_7(i, ...)  ((1ULL << (i)) | __M0_BITS_6(__VA_ARGS__))
#define __M0_BITS_8(i, ...)  ((1ULL << (i)) | __M0_BITS_7(__VA_ARGS__))

M0_INTERNAL const char *m0_bool_to_str(bool b);

/**
 * Extracts the file name, relative to a mero sources directory, from a
 * full-path file name. A mero source directory is detected by a name
 * "mero/".
 *
 * For example, given the following full-path file name:
 *
 *     /path/to/mero/lib/ut/finject.c
 *
 * A short file name, relative to the "mero/" directory, is:
 *
 *     lib/ut/finject.c
 *
 * @bug {
 *     This function doesn't search for the rightmost occurrence of "mero/"
 *     in a file path, if "mero/" encounters several times in the path the first
 *     one will be picked up:
 *
 *       /path/to/mero/fs/mero/lib/misc.h => fs/mero/lib/misc.h
 * }
 *
 * @param   fname  full path
 *
 * @return  short file name - a pointer inside fname string to the remaining
 *          file path, after mero source directory;
 *          if short file name cannot be found, then full fname is returned.
 */
M0_INTERNAL const char *m0_short_file_name(const char *fname);

/* strtoull for user- and kernel-space */
M0_INTERNAL uint64_t m0_strtou64(const char *str, char **endptr, int base);

/* strtoul for user- and kernel-space */
M0_INTERNAL uint32_t m0_strtou32(const char *str, char **endptr, int base);

/* __MERO_LIB_MISC_H__ */
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
