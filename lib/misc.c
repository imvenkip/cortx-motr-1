/* -*- C -*- */

#include "lib/types.h"
#include "lib/cdefs.h"
#include "lib/assert.h"
#include "lib/arith.h"      /* C2_3WAY */

#ifdef __KERNEL__
# include <linux/module.h>
# include <linux/kernel.h>

# define UINT64_MAX (uint64_t)(~((uint64_t) 0))  /* 0xFFFFFFFFFFFFFFFF */
#else
# include <string.h>         /* strlen */
#endif

void __dummy_function(void)
{
}

/* No padding. */
C2_BASSERT(sizeof(struct c2_uint128) == 16);

bool c2_uint128_eq(const struct c2_uint128 *u0, const struct c2_uint128 *u1)
{
	return c2_uint128_cmp(u0, u1) == 0;
}
C2_EXPORTED(c2_uint128_eq);

int c2_uint128_cmp(const struct c2_uint128 *u0, const struct c2_uint128 *u1)
{
	return C2_3WAY(u0->u_hi, u1->u_hi) ?: C2_3WAY(u0->u_lo, u1->u_lo);
}
C2_EXPORTED(c2_uint128_cmp);

#ifdef __KERNEL__
uint64_t c2_rnd(uint64_t max, uint64_t *prev)
{
        uint64_t result;
        /* Constants of the generator taken from glibc sources: nrand48_r.c@__nrand48_r() */
        result = *prev = *prev * 0x5DEECE66DULL + 0xB;

        /* ret = (result>>16) * max / ((~0UL) >> 16); */
        /* printf("rnd: res=%lu, max=%lu, ret=%lu, lu=%lu\n", */
        /*        result, max, ret, ~0UL); */

        /*Take value from higher 48 bits */
        return (result >> 16) * max / ((~0UL) >> 16);
}
C2_EXPORTED(c2_rnd);

#else /* __KERNEL__ */
uint64_t c2_rnd(uint64_t max, uint64_t *prev)
{
	/*
	 * Linear congruential generator with constants from TAOCP MMIX.
	 * http://en.wikipedia.org/wiki/Linear_congruential_generator
	 */
	double result;
	result = *prev = *prev * 6364136223846793005ULL + 1442695040888963407;
	/*
	 * Use higher bits of *prev to generate return value, because they are
	 * more random.
	 */
	return result * max / (1.0 + ~0ULL); 
}
#endif /* __KERNEL__ */

uint64_t c2_gcd64(uint64_t p, uint64_t q)
{
	uint64_t t;

	while (q != 0) {
		t = p % q;
		p = q;
		q = t;
	}
	return p;
}
C2_EXPORTED(c2_gcd64);

static uint64_t c2u64(const unsigned char *s)
{
	uint64_t v;
	int      i;

	for (v = 0, i = 0; i < 8; ++i)
		v |= ((uint64_t)s[i]) << (64 - 8 - i * 8);
	return v;
}

void c2_uint128_init(struct c2_uint128 *u128, const char *magic)
{
	C2_ASSERT(strlen(magic) == sizeof *u128);
	u128->u_hi = c2u64((const unsigned char *)magic);
	u128->u_lo = c2u64((const unsigned char *)magic + 8);
}
C2_EXPORTED(c2_uint128_init);

enum {
	C2_MOD_SAFE_LIMIT = UINT64_MAX/32
};

static int64_t getdelta(uint64_t x0, uint64_t x1)
{
	int64_t delta;

	delta = (int64_t)x0 - (int64_t)x1;
	C2_ASSERT(delta < C2_MOD_SAFE_LIMIT && -delta < C2_MOD_SAFE_LIMIT);
	return delta;
}

bool c2_mod_gt(uint64_t x0, uint64_t x1)
{
	return getdelta(x0, x1) > 0;
}

bool c2_mod_ge(uint64_t x0, uint64_t x1)
{
	return getdelta(x0, x1) >= 0;
}

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
