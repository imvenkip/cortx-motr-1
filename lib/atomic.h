/* -*- C -*- */
#ifndef __COLIBRI_LIB_ATOMIC_H__
#define __COLIBRI_LIB_ATOMIC_H__

#include "lib/cdefs.h"

/**
 atomic counter
 */
struct atomic {
	long value;
};
typedef struct atomic atomic_t;


/**
 set value to atomic counter

 @param a pointer to atomic counter
 @param num value to set

 @return none
 */
static inline
void c2_atomic_set(atomic_t *a, int num)
{
	a->value = num;
}

/**
 atomically increment counter

 @param a pointer to atomic counter

 @return none
 */
static inline
void c2_atomic_inc(atomic_t *a)
{
	a->value ++;
}

/**
 atomically increment counter and return result

 @param a pointer to atomic counter

 @return new value of atomic counter
 */
static inline
long c2_atomic_inc_and_test(atomic_t *a)
{
	return a->value += 1;
}

/**
 atomically decrement counter

 @param a pointer to atomic counter

 @return none
 */
static inline
void c2_atomic_dec(atomic_t *a)
{
	a->value --;
}


/**
 atomically decrement counter and return result

 @param a pointer to atomic counter

 @return new value of atomic counter
 */
static inline
long c2_atomic_dec_and_test(atomic_t *a)
{
	return a->value -= 1;
}

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
