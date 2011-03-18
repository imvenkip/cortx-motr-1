/* -*- C -*- */
#ifndef __COLIBRI_LIB_LINUX_KERNEL_ATOMIC64_H__
#define __COLIBRI_LIB_LINUX_KERNEL_ATOMIC64_H__

#include "lib/types.h"
#include "lib/cdefs.h"
#include "lib/assert.h"

/**
   @addtogroup atomic

   Implementation of atomic operations for Linux user space uses x86_64 assembly
   language instructions (with gcc syntax). "Lock" prefix is used
   everywhere---no optimisation for non-SMP configurations in present.
 */

#ifdef PREFIX
#undef PREFIX
#endif

#define PREFIX static inline

struct c2_atomic64 {
	atomic64_t a_value;
};

PREFIX void c2_atomic64_set(struct c2_atomic64 *a, int64_t num)
{
	C2_CASSERT(sizeof a->a_value == sizeof num);

	atomic64_set(&a->a_value, num);
}

/**
   Returns value of an atomic counter.
 */
PREFIX int64_t c2_atomic64_get(const struct c2_atomic64 *a)
{
	return	atomic64_read(&a->a_value);
}

/**
 atomically increment counter

 @param a pointer to atomic counter

 @return none
 */
PREFIX void c2_atomic64_inc(struct c2_atomic64 *a)
{
	atomic64_inc(&a->a_value);
}

/**
 atomically decrement counter

 @param a pointer to atomic counter

 @return none
 */
PREFIX void c2_atomic64_dec(struct c2_atomic64 *a)
{
	atomic64_dec(&a->a_value);
}

/**
   Atomically adds given amount to a counter
 */
PREFIX void c2_atomic64_add(struct c2_atomic64 *a, int64_t num)
{
	atomic64_add(num, &a->a_value);
}

/**
   Atomically subtracts given amount from a counter
 */
PREFIX void c2_atomic64_sub(struct c2_atomic64 *a, int64_t num)
{
	atomic64_sub(num, &a->a_value);
}


/**
 atomically increment counter and return result

 @param a pointer to atomic counter

 @return new value of atomic counter
 */
PREFIX int64_t c2_atomic64_add_return(struct c2_atomic64 *a, int64_t delta)
{
	return atomic64_add_return(delta, &a->a_value);
}

/**
 atomically decrement counter and return result

 @param a pointer to atomic counter

 @return new value of atomic counter
 */
PREFIX int64_t c2_atomic64_sub_return(struct c2_atomic64 *a, int64_t delta)
{
	return atomic64_sub_return(delta, &a->a_value);
}

PREFIX bool c2_atomic64_inc_and_test(struct c2_atomic64 *a)
{
	return atomic64_inc_and_test(&a->a_value);
}

PREFIX bool c2_atomic64_dec_and_test(struct c2_atomic64 *a)
{
	return atomic64_dec_and_test(&a->a_value);
}

/** @} end of atomic group */

/* __COLIBRI_LIB_LINUX_KERNEL_ATOMIC64_H__ */
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
