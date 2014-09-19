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
 *		    Alexey Lyashkov <Alexey_Lyashkov@xyratex.com>
 * Original creation date: 18-Jun-2010
 */

#pragma once

#ifndef __MERO_LIB_MISC_H__
#define __MERO_LIB_MISC_H__

#ifdef __KERNEL__
#  include <linux/string.h>       /* memset, strstr */
#  include <linux/bitops.h>       /* ffs */
#  include "lib/linux_kernel/misc.h"
#else
#  include <string.h>             /* memset, ffs, strstr */
#  include "lib/user_space/misc.h"
#endif
#include "lib/types.h"
#include "lib/assert.h"           /* M0_CASSERT */

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

#define M0_SET0(obj)                     \
({                                       \
	M0_CASSERT(!m0_is_array(obj));   \
	memset((obj), 0, sizeof *(obj)); \
})

#define M0_IS0(obj) m0_forall(i, sizeof *(obj), ((char *)obj)[i] == 0)

#define M0_SET_ARR0(arr)                \
({                                      \
	M0_CASSERT(m0_is_array(arr));   \
	memset((arr), 0, sizeof (arr)); \
})

/** Returns the number of array elements that satisfy given criteria. */
#define m0_count(var, nr, ...)                     \
({                                                 \
	unsigned __nr = (nr);                      \
	unsigned var;                              \
	unsigned count;                            \
						   \
	for (count = var = 0; var < __nr; ++var) { \
		if (__VA_ARGS__)                   \
			++count;                   \
	}                                          \
	count;                                     \
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
#define m0_forall(var, nr, ...)                                 \
({                                                              \
	unsigned __nr = (nr);                                   \
	unsigned var;                                           \
								\
	for (var = 0; var < __nr && ({ __VA_ARGS__ ; }); ++var) \
		;                                               \
	var == __nr;                                            \
})

/**
 * Returns a disjunction (logical OR) of an expression evaluated over a range.
 *
 * @code
 * bool haystack_contains(int needle)
 * {
 *         return m0_exists(i, ARRAY_SIZE(haystack), haystack[i] == needle);
 * }
 * @endcode
 *
 * @see m0_forall()
 */
#define m0_exists(var, nr, ...) !m0_forall(var, (nr), !(__VA_ARGS__))

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
   @code (1ULL << FOO_ACTIVE) | (1ULL << FOO_FAILED) @endcode

   @note M0_BITS() macro with no parameters causes compilation failure.
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

#define IS_IN_ARRAY(idx, array)                     \
({                                                  \
	M0_CASSERT(m0_is_array(array));             \
	((unsigned long)(idx)) < ARRAY_SIZE(array); \
})

M0_INTERNAL bool m0_elems_are_unique(const void *array, unsigned nr_elems,
				     size_t elem_size);

#define M0_AMB(obj, ptr, field)					\
({								\
	(obj) = container_of((ptr), typeof(*(obj)), field);	\
})

#define M0_MEMBER_PTR(ptr, member)		\
({						\
	typeof(ptr) __ptr = (ptr);		\
	__ptr == NULL ? NULL : &__ptr->member;	\
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
#define M0_MAGIX_OFFSET(type, field) \
M0_FIELD_IS(type, field, uint64_t) ? \
	offsetof(type, field) :      \
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
 * Resolve hostname.
 */
M0_INTERNAL int m0_host_resolve(const char *name, char *buf, size_t bufsiz);

#define M0_UNUSED __attribute__((unused))

M0_INTERNAL uint32_t m0_no_of_bits_set(uint64_t val);

M0_INTERNAL unsigned int
m0_full_name_hash(const unsigned char *name, unsigned int len);

#endif /* __MERO_LIB_MISC_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
