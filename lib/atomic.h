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

#ifndef __MERO_LIB_ATOMIC_H__
#define __MERO_LIB_ATOMIC_H__

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
struct m0_atomic64;

/**
   Assigns a value to a counter.
 */
static inline void m0_atomic64_set(struct m0_atomic64 *a, int64_t num);

/**
   Returns value of an atomic counter.
 */
static inline int64_t m0_atomic64_get(const struct m0_atomic64 *a);

/**
   Atomically increments a counter.
 */
static inline void m0_atomic64_inc(struct m0_atomic64 *a);

/**
   Atomically decrements a counter.
 */
static inline void m0_atomic64_dec(struct m0_atomic64 *a);

/**
   Atomically adds given amount to a counter.
 */
static inline void m0_atomic64_add(struct m0_atomic64 *a, int64_t num);

/**
   Atomically subtracts given amount from a counter.
 */
static inline void m0_atomic64_sub(struct m0_atomic64 *a, int64_t num);

/**
   Atomically increments a counter and returns the result.
 */
static inline int64_t m0_atomic64_add_return(struct m0_atomic64 *a,
						  int64_t d);

/**
   Atomically decrements a counter and returns the result.
 */
static inline int64_t m0_atomic64_sub_return(struct m0_atomic64 *a,
						  int64_t d);

/**
   Atomically increments a counter and returns true iff the result is 0.
 */
static inline bool m0_atomic64_inc_and_test(struct m0_atomic64 *a);

/**
   Atomically decrements a counter and returns true iff the result is 0.
 */
static inline bool m0_atomic64_dec_and_test(struct m0_atomic64 *a);

/**
   Atomic compare-and-swap: compares value stored in @loc with @old and, if
   equal, replaces it with @new, all atomic w.r.t. concurrent accesses to @loc.

   Returns true iff new value was installed.
 */
static inline bool m0_atomic64_cas(int64_t * loc, int64_t old,
					int64_t new);

/**
   Atomic compare-and-swap for pointers.

   @see m0_atomic64_cas().
 */
static inline bool m0_atomic64_cas_ptr(void **loc, void *old, void *new)
{
	M0_CASSERT(sizeof loc == sizeof(int64_t *));
	M0_CASSERT(sizeof old == sizeof(int64_t));

	return m0_atomic64_cas((int64_t *)loc, (int64_t)old, (int64_t)new);
}

#define M0_ATOMIC64_CAS(loc, old, new)					\
({									\
	M0_CASSERT(__builtin_types_compatible_p(typeof(*(loc)), typeof(old))); \
	M0_CASSERT(__builtin_types_compatible_p(typeof(old), typeof(new))); \
	m0_atomic64_cas_ptr((void **)(loc), old, new);			\
})

/**
   Hardware memory barrier. Forces strict CPU ordering.
 */
static inline void m0_mb(void);

/** @} end of atomic group */

/* __MERO_LIB_ATOMIC_H__ */
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
