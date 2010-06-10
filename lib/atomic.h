/* -*- C -*- */
#ifndef __COLIBRI_LIB_ATOMIC_H__
#define __COLIBRI_LIB_ATOMIC_H__

#include "cdefs.h"
#include "assert.h"
#include "user_x86_64_atomic.h"

/**
   @defgroup atomic

   Atomic operations on 64bit quantities.

   Implementation of these is platform-specific.
 */

/**
   atomic counter
 */
struct c2_atomic64;

/**
   Assigns a value to a counter.
 */
PREFIX void c2_atomic64_set(struct c2_atomic64 *a, int64_t num);

/**
   Returns value of an atomic counter.
 */
PREFIX int64_t c2_atomic64_get(const struct c2_atomic64 *a);

/**
   Atomically increments a counter.
 */
PREFIX void c2_atomic64_inc(struct c2_atomic64 *a);

/**
   Atomically decrements a counter.
 */
PREFIX void c2_atomic64_dec(struct c2_atomic64 *a);

/**
   Atomically adds given amount to a counter.
 */
PREFIX void c2_atomic64_add(struct c2_atomic64 *a, int64_t num);

/**
   Atomically subtracts given amount from a counter.
 */
PREFIX void c2_atomic64_sub(struct c2_atomic64 *a, int64_t num);

/**
   Atomically increments a counter and returns the result.
 */
PREFIX int64_t c2_atomic64_add_return(struct c2_atomic64 *a, int64_t d);

/**
   Atomically decrements a counter and returns the result.
 */
PREFIX int64_t c2_atomic64_sub_return(struct c2_atomic64 *a, int64_t d);

/**
   Atomically increments a counter and returns true iff the result is 0.
 */
PREFIX bool c2_atomic64_inc_and_test(struct c2_atomic64 *a);

/**
   Atomically decrements a counter and returns true iff the result is 0.
 */
PREFIX bool c2_atomic64_dec_and_test(struct c2_atomic64 *a);

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
