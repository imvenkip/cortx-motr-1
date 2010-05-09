/* -*- C -*- */
#ifndef __COLIBRI_LIB_ATOMIC_H__
#define __COLIBRI_LIB_ATOMIC_H__

#include "cdefs.h"
#include "asrt.h"
#include "user_x86_64_atomic.h"

/**
   @defgroup atomic

   Atomic operations on 64bit quantities.
 */

/**
   atomic counter
 */
struct c2_atomic64;

/**
   set value to atomic counter

   @param a pointer to atomic counter
   @param num value to set

   @return none
 */
PREFIX void c2_atomic64_set(struct c2_atomic64 *a, int64_t num);

/**
   Returns value of an atomic counter.
 */
PREFIX int64_t c2_atomic64_get(const struct c2_atomic64 *a);

/**
   atomically increment counter

   @param a pointer to atomic counter
   @return none
 */
PREFIX void c2_atomic64_inc(struct c2_atomic64 *a);

/**
   atomically decrement counter

   @param a pointer to atomic counter

   @return none
 */
PREFIX void c2_atomic64_dec(struct c2_atomic64 *a);

/**
   Atomically adds given amount to a counter
 */
PREFIX void c2_atomic64_add(struct c2_atomic64 *a, int64_t num);

/**
   Atomically subtracts given amount from a counter
 */
PREFIX void c2_atomic64_sub(struct c2_atomic64 *a, int64_t num);

/**
   atomically increment counter and return result

   @param a pointer to atomic counter

   @return new value of atomic counter
 */
PREFIX int64_t c2_atomic64_add_return(struct c2_atomic64 *a, int64_t d);

/**
 atomically decrement counter and return result

 @param a pointer to atomic counter

 @return new value of atomic counter
 */
PREFIX int64_t c2_atomic64_sub_return(struct c2_atomic64 *a, int64_t d);

PREFIX bool c2_atomic64_inc_and_test(struct c2_atomic64 *a);
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
