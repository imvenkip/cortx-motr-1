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
	uint64_t result;

	*prev = result = *prev * 6364136223846793005ULL + 1442695040888963407;
	return result % max;
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

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
