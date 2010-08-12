/* -*- C -*- */
#ifndef __COLIBRI_LIB_USER_X86_64_ATOMIC_H__
#define __COLIBRI_LIB_USER_X86_64_ATOMIC_H__

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
	long a_value;
};

PREFIX void c2_atomic64_set(struct c2_atomic64 *a, int64_t num)
{
	C2_CASSERT(sizeof a->a_value == sizeof num);

	a->a_value = num;
}

/**
   Returns value of an atomic counter.
 */
PREFIX int64_t c2_atomic64_get(const struct c2_atomic64 *a)
{
	return a->a_value;
}

/**
 atomically increment counter

 @param a pointer to atomic counter

 @return none
 */
PREFIX void c2_atomic64_inc(struct c2_atomic64 *a)
{
	asm volatile("lock incq %0"
		     : "=m" (a->a_value)
		     : "m" (a->a_value));
}

/**
 atomically decrement counter

 @param a pointer to atomic counter

 @return none
 */
PREFIX void c2_atomic64_dec(struct c2_atomic64 *a)
{
	asm volatile("lock decq %0"
		     : "=m" (a->a_value)
		     : "m" (a->a_value));
}

/**
   Atomically adds given amount to a counter
 */
PREFIX void c2_atomic64_add(struct c2_atomic64 *a, int64_t num)
{
	asm volatile("lock addq %1,%0"
		     : "=m" (a->a_value)
		     : "er" (num), "m" (a->a_value));
}

/**
   Atomically subtracts given amount from a counter
 */
PREFIX void c2_atomic64_sub(struct c2_atomic64 *a, int64_t num)
{
	asm volatile("lock subq %1,%0"
		     : "=m" (a->a_value)
		     : "er" (num), "m" (a->a_value));
}


/**
 atomically increment counter and return result

 @param a pointer to atomic counter

 @return new value of atomic counter
 */
PREFIX int64_t c2_atomic64_add_return(struct c2_atomic64 *a, int64_t delta)
{
	long result;

	result = delta;
	asm volatile("lock xaddq %0, %1;"
		     : "+r" (delta), "+m" (a->a_value)
		     : : "memory");
	return delta + result;
}

/**
 atomically decrement counter and return result

 @param a pointer to atomic counter

 @return new value of atomic counter
 */
PREFIX int64_t c2_atomic64_sub_return(struct c2_atomic64 *a, int64_t delta)
{
	return c2_atomic64_add_return(a, -delta);
}

PREFIX bool c2_atomic64_inc_and_test(struct c2_atomic64 *a)
{
	unsigned char result;

	asm volatile("lock incq %0; sete %1"
		     : "=m" (a->a_value), "=qm" (result)
		     : "m" (a->a_value) : "memory");
	return result != 0;
}

PREFIX bool c2_atomic64_dec_and_test(struct c2_atomic64 *a)
{
	unsigned char result;

	asm volatile("lock decq %0; sete %1"
		     : "=m" (a->a_value), "=qm" (result)
		     : "m" (a->a_value) : "memory");
	return result != 0;
}

/** @} end of atomic group */

/* __COLIBRI_LIB_USER_X86_64_ATOMIC_H__ */
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
