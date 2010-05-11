/* -*- C -*- */
#ifndef __COLIBRI_LIB_ATOMIC_H__
#define __COLIBRI_LIB_ATOMIC_H__

#include "lib/cdefs.h"
#include "lib/assert.h"

/**
 atomic counter
 */
struct c2_atomic64 {
	long a_value;
};

#define C2_ATOMIC64_INIT(val) { \
        .a_value = (val)        \
}

/**
 set value to atomic counter

 @param a pointer to atomic counter
 @param num value to set

 @return none
 */
static inline void c2_atomic64_set(struct c2_atomic64 *a, int64_t num)
{
	C2_CASSERT(sizeof a->a_value == sizeof num);

	a->a_value = num;
}

/**
   Returns value of an atomic counter.
 */
static inline int64_t c2_atomic64_get(const struct c2_atomic64 *a)
{
	return a->a_value;
}

/**
 atomically increment counter

 @param a pointer to atomic counter

 @return none
 */
static inline void c2_atomic64_inc(struct c2_atomic64 *a)
{
	a->a_value ++;
}

/**
 atomically decrement counter

 @param a pointer to atomic counter

 @return none
 */
static inline void c2_atomic64_dec(struct c2_atomic64 *a)
{
	a->a_value --;
}

/**
   Atomically adds given amount to a counter
 */
static inline void c2_atomic64_add(struct c2_atomic64 *a, int64_t num)
{
	a->a_value += num;
}

/**
   Atomically subtracts given amount from a counter
 */
static inline void c2_atomic64_sub(struct c2_atomic64 *a, int64_t num)
{
	a->a_value -= num;
}


/**
 atomically increment counter and return result

 @param a pointer to atomic counter

 @return new value of atomic counter
 */
static inline int64_t c2_atomic64_add_return(struct c2_atomic64 *a, int64_t d)
{
	return a->a_value += d;
}

/**
 atomically decrement counter and return result

 @param a pointer to atomic counter

 @return new value of atomic counter
 */
static inline int64_t c2_atomic64_sub_return(struct c2_atomic64 *a, int64_t d)
{
	return a->a_value -= d;
}

static inline bool c2_atomic64_inc_and_test(struct c2_atomic64 *a)
{
	return c2_atomic64_add_return(a, 1) == 0;
}

static inline bool c2_atomic64_dec_and_test(struct c2_atomic64 *a)
{
	return c2_atomic64_sub_return(a, 1) == 0;
}

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
