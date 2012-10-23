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
#include <string.h>               /* memset, ffs, strstr */
#else
#include <linux/string.h>         /* memset, strstr */
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
 * bool foo_invariant(const struct foo *f)
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

const char *c2_bool_to_str(bool b);

/**
 * Extracts the file name, relative to a colibri sources directory, from a
 * full-path file name. A colibri source directory is detected by a name
 * "core/".
 *
 * For example, given the following full-path file name:
 *
 *     /data/colibri/core/lib/ut/finject.c
 *
 * A short file name, relative to the "core/" directory, is:
 *
 *     lib/ut/finject.c
 *
 * If there is a "core/build_kernel_modules/" directory in the file's full path,
 * then short file name is stripped relative to this directory:
 *
 *     /data/colibri/core/build_kernel_modules/rpc/packet.c => rpc/packet.c
 *
 * @bug {
 *     This function doesn't search for the right-most occurrence of "core/"
 *     in a file path, if "core/" encounters several times in the path the first
 *     one will be picked up:
 *
 *       /prj/core/fs/colibri/core/lib/misc.h => fs/colibri/core/lib/misc.h
 * }
 *
 * @param   fname  full path
 *
 * @return  short file name - a pointer inside fname string to the remaining
 *          file path, after colibri source directory;
 *          if short file name cannot be found, then full fname is returned.
 */
const char *c2_short_file_name(const char *fname);

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
