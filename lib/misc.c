/* -*- C -*- */

#include <string.h> /* memcmp() */

#include "lib/cdefs.h"
#include "lib/assert.h"

void __dummy_function(void)
{
}

/* No padding. */
C2_BASSERT(sizeof(struct c2_uint128) == 16);

bool c2_uint128_eq(const struct c2_uint128 *u0, const struct c2_uint128 *u1)
{
	return c2_uint128_cmp(u0, u1) == 0;
}

int c2_uint128_cmp(const struct c2_uint128 *u0, const struct c2_uint128 *u1)
{
	return memcmp(u0, u1, sizeof *u0);
}

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

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
