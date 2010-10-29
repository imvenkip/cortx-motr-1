/* -*- C -*- */

#include <string.h>         /* strlen */

#include "lib/types.h"
#include "lib/cdefs.h"
#include "lib/assert.h"
#include "lib/arith.h"      /* C2_3WAY */
#include "lib/adt.h"


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
	return C2_3WAY(u0->u_hi, u1->u_hi) ?: C2_3WAY(u0->u_lo, u1->u_lo);
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

void *c2_bitstring_buf_get(c2_bitstring *c)
{
	return c->b_addr;
}

void c2_bitstring_buf_set(c2_bitstring *c, void *data)
{
	c->b_addr = data;
}

c2_bcount_t c2_bitstring_len_get(c2_bitstring *c)
{
	return c->b_nob;
}

void c2_bitstring_len_set(c2_bitstring *c, c2_bcount_t len)
{
	c->b_nob = len;
}

/**
   String-like compare: alphanumeric for the length of the shortest string.
   Shorter strings precede longer strings.
   Strings may contain embedded NULLs.
 */
int c2_bitstring_cmp(const c2_bitstring *c1, const c2_bitstring *c2)
{
        char *s1 = c1->b_addr;
        char *s2 = c2->b_addr;
        unsigned char uc1 = 0, uc2;
        int rc;

        /* end of compare */
        uc2 = c1->b_nob < c2->b_nob ? c1->b_nob : c2->b_nob;
        /* first diff */
        while (*s1 == *s2 && uc1 < uc2) {
                s1++;
                s2++;
                uc1++;
        }
        /* Compare the characters as unsigned char and
         return the difference.  */
        uc1 = (*(unsigned char *) s1);
        uc2 = (*(unsigned char *) s2);

        if ((rc = (uc1 < uc2) ? -1 : (uc1 > uc2)))
                return rc;

        /* Everything matches through the shortest string */
        return (c1->b_nob < c2->b_nob) ? -1 : (c1->b_nob > c2->b_nob);
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
