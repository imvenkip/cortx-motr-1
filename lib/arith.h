/* -*- C -*- */

#ifndef __COLIBRI_LIB_ARITH_H__
#define __COLIBRI_LIB_ARITH_H__

#include "cdefs.h"

/**
   @defgroup arith Miscellaneous arithmetic functions.
   @{
 */

static inline int32_t min32(int32_t a, int32_t b)
{
	return a < b ? a : b;
}

static inline int32_t max32(int32_t a, int32_t b)
{
	return a > b ? a : b;
}

static inline int64_t min64(int64_t a, int64_t b)
{
	return a < b ? a : b;
}

static inline int64_t max64(int64_t a, int64_t b)
{
	return a > b ? a : b;
}

static inline uint32_t min32u(uint32_t a, uint32_t b)
{
	return a < b ? a : b;
}

static inline uint32_t max32u(uint32_t a, uint32_t b)
{
	return a > b ? a : b;
}

static inline uint64_t min64u(uint64_t a, uint64_t b)
{
	return a < b ? a : b;
}

static inline uint64_t max64u(uint32_t a, uint64_t b)
{
	return a > b ? a : b;
}

#define min_t(t, a, b) ({			\
	t __a = (a);				\
	t __b = (b);				\
	__a < __b ? __a : __b;			\
})

#define max_t(t, a, b) ({			\
	t __a = (a);				\
	t __b = (b);				\
	__a > __b ? __a : __b;			\
})

#define min_check(a, b) ({			\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	(void)(&__a == &__b);			\
	__a < __b ? __a : __b;			\
})

#define max_check(a, b) ({			\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	(void)(&__a == &__b);			\
	__a > __b ? __a : __b;			\
})

/**
   A very simple and fast re-entrant PRNG from Knuth.

   Generates a pseudo-random number using "seed" and stores the number back in
   seed. Result is no greater than max.

   @post result < max
 */
uint64_t c2_rnd(uint64_t max, uint64_t *seed);

/**
   Greatest common divisor.
 */
uint64_t c2_gcd64(uint64_t p, uint64_t q);

/** @} end of arith group */

/* __COLIBRI_LIB_ARITH_H__ */
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
