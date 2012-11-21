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
 * Original creation date: 04/08/2010
 */

#pragma once

#ifndef __COLIBRI_LIB_ATOMIC_H__
#define __COLIBRI_LIB_ATOMIC_H__

#include "cdefs.h"
#include "assert.h"

#ifndef __KERNEL__
# ifdef ENABLE_SYNC_ATOMIC
# include "user_space/__sync_atomic.h"
# else
# include "user_space/user_x86_64_atomic.h"
# endif /* ENABLE_SYNC_ATOMIC */
#else
#include "linux_kernel/atomic64.h"
#endif /* __KERNEL__ */

/**
   @defgroup atomic

   Atomic operations on 64bit quantities.

   Implementation of these is platform-specific.

   @{
 */

/**
   atomic counter
 */
struct c2_atomic64;

/**
   Assigns a value to a counter.
 */
static inline void c2_atomic64_set(struct c2_atomic64 *a, int64_t num);

/**
   Returns value of an atomic counter.
 */
static inline int64_t c2_atomic64_get(const struct c2_atomic64 *a);

/**
   Atomically increments a counter.
 */
static inline void c2_atomic64_inc(struct c2_atomic64 *a);

/**
   Atomically decrements a counter.
 */
static inline void c2_atomic64_dec(struct c2_atomic64 *a);

/**
   Atomically adds given amount to a counter.
 */
static inline void c2_atomic64_add(struct c2_atomic64 *a, int64_t num);

/**
   Atomically subtracts given amount from a counter.
 */
static inline void c2_atomic64_sub(struct c2_atomic64 *a, int64_t num);

/**
   Atomically increments a counter and returns the result.
 */
static inline int64_t c2_atomic64_add_return(struct c2_atomic64 *a,
						  int64_t d);

/**
   Atomically decrements a counter and returns the result.
 */
static inline int64_t c2_atomic64_sub_return(struct c2_atomic64 *a,
						  int64_t d);

/**
   Atomically increments a counter and returns true iff the result is 0.
 */
static inline bool c2_atomic64_inc_and_test(struct c2_atomic64 *a);

/**
   Atomically decrements a counter and returns true iff the result is 0.
 */
static inline bool c2_atomic64_dec_and_test(struct c2_atomic64 *a);

/**
   Atomic compare-and-swap: compares value stored in @loc with @old and, if
   equal, replaces it with @new, all atomic w.r.t. concurrent accesses to @loc.

   Returns true iff new value was installed.
 */
static inline bool c2_atomic64_cas(int64_t * loc, int64_t old,
					int64_t new);

/**
   Atomic compare-and-swap for pointers.

   @see c2_atomic64_cas().
 */
static inline bool c2_atomic64_cas_ptr(void **loc, void *old, void *new)
{
	C2_CASSERT(sizeof loc == sizeof(int64_t *));
	C2_CASSERT(sizeof old == sizeof(int64_t));

	return c2_atomic64_cas((int64_t *)loc, (int64_t)old, (int64_t)new);
}

#define C2_ATOMIC64_CAS(loc, old, new)					\
({									\
	C2_CASSERT(__builtin_types_compatible_p(typeof(*(loc)), typeof(old))); \
	C2_CASSERT(__builtin_types_compatible_p(typeof(old), typeof(new))); \
	c2_atomic64_cas_ptr((void **)(loc), old, new);			\
})

/** @} end of atomic group */

/* __COLIBRI_LIB_ATOMIC_H__ */
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
