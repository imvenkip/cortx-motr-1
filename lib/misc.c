/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 06/18/2010
 */

#include "lib/types.h"
#include "lib/assert.h"
#include "lib/arith.h"      /* M0_3WAY */
#include "lib/misc.h"

#ifndef __KERNEL__
#include <netdb.h>  /* gethostbyname_r */
#include <arpa/inet.h> /* inet_ntoa, inet_ntop */
#include <unistd.h> /* gethostname */
#include <errno.h>
#include <limits.h>	    /* CHAR_BIT */
#endif

void __dummy_function(void)
{
}

/* No padding. */
M0_BASSERT(sizeof(struct m0_uint128) == 16);

M0_INTERNAL bool m0_uint128_eq(const struct m0_uint128 *u0,
			       const struct m0_uint128 *u1)
{
	return m0_uint128_cmp(u0, u1) == 0;
}

M0_INTERNAL int m0_uint128_cmp(const struct m0_uint128 *u0,
			       const struct m0_uint128 *u1)
{
	return M0_3WAY(u0->u_hi, u1->u_hi) ?: M0_3WAY(u0->u_lo, u1->u_lo);
}

M0_INTERNAL void m0_uint128_add(struct m0_uint128 *res,
				const struct m0_uint128 a,
				const struct m0_uint128 b)
{
	res->u_lo = a.u_lo + b.u_lo;
	res->u_hi = a.u_hi + b.u_hi + (res->u_lo < a.u_lo);
}

M0_INTERNAL void m0_uint128_mul64(struct m0_uint128 *res, uint64_t a,
				  uint64_t b)
{
	uint64_t a_lo = a & UINT32_MAX;
	uint64_t a_hi = a >> 32;
	uint64_t b_lo = b & UINT32_MAX;
	uint64_t b_hi = b >> 32;
	uint64_t c;

	M0_CASSERT((sizeof a) * CHAR_BIT == 64);

	/*
	 * a * b = a_hi * b_hi * (1 << 64) +
	 *	   a_lo * b_lo +
	 *	   a_lo * b_hi * (1 << 32) +
	 *	   a_hi * b_lo * (1 << 32)
	 */
	*res = M0_UINT128(a_hi * b_hi, a_lo * b_lo);
	c = a_lo * b_hi;
	m0_uint128_add(res, *res, M0_UINT128(c >> 32, (c & UINT32_MAX) << 32));
	c = a_hi * b_lo;
	m0_uint128_add(res, *res, M0_UINT128(c >> 32, (c & UINT32_MAX) << 32));
}

#if 0
uint64_t m0_rnd(uint64_t max, uint64_t *prev)
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
#endif

uint64_t m0_rnd(uint64_t max, uint64_t *prev)
{
        uint64_t result;
        /* Uses the same algorithm as GNU libc */
        result = *prev = *prev * 0x5DEECE66DULL + 0xB;

	/* PRNG generates 48-bit values only */
	M0_ASSERT((max >> 48) == 0);
        /*Take value from higher 48 bits */
        return (result >> 16) * max / ((~0UL) >> 16);
}
M0_EXPORTED(m0_rnd);

M0_INTERNAL uint64_t m0_gcd64(uint64_t p, uint64_t q)
{
	uint64_t t;

	while (q != 0) {
		t = p % q;
		p = q;
		q = t;
	}
	return p;
}

static uint64_t m0u64(const unsigned char *s)
{
	uint64_t v;
	int      i;

	for (v = 0, i = 0; i < 8; ++i)
		v |= ((uint64_t)s[i]) << (64 - 8 - i * 8);
	return v;
}

M0_INTERNAL void m0_uint128_init(struct m0_uint128 *u128, const char *magic)
{
	M0_ASSERT(strlen(magic) == sizeof *u128);
	u128->u_hi = m0u64((const unsigned char *)magic);
	u128->u_lo = m0u64((const unsigned char *)magic + 8);
}

enum {
	M0_MOD_SAFE_LIMIT = UINT64_MAX/32
};

static int64_t getdelta(uint64_t x0, uint64_t x1)
{
	int64_t delta;

	delta = (int64_t)x0 - (int64_t)x1;
	M0_ASSERT(delta < M0_MOD_SAFE_LIMIT && -delta < M0_MOD_SAFE_LIMIT);
	return delta;
}

M0_INTERNAL bool m0_mod_gt(uint64_t x0, uint64_t x1)
{
	return getdelta(x0, x1) > 0;
}

M0_INTERNAL bool m0_mod_ge(uint64_t x0, uint64_t x1)
{
	return getdelta(x0, x1) >= 0;
}

M0_INTERNAL uint64_t m0_round_up(uint64_t val, uint64_t size)
{
	M0_PRE(m0_is_po2(size));
	return (val + size - 1) & ~(size - 1) ;
}

M0_INTERNAL uint64_t m0_round_down(uint64_t val, uint64_t size)
{
	M0_PRE(m0_is_po2(size));
	return val & ~(size - 1);
}

/*
 * Check that ergo() and equi() macros are really what they pretend to be.
 */

M0_BASSERT(ergo(false, false) == true);
M0_BASSERT(ergo(false, true)  == true);
M0_BASSERT(ergo(true,  false) == false);
M0_BASSERT(ergo(true,  true)  == true);

M0_BASSERT(equi(false, false) == true);
M0_BASSERT(equi(false, true)  == false);
M0_BASSERT(equi(true,  false) == false);
M0_BASSERT(equi(true,  true)  == true);

M0_INTERNAL const char *m0_bool_to_str(bool b)
{
	return b ? "true" : "false";
}

M0_INTERNAL const char *m0_short_file_name(const char *fname)
{
	static const char  top_src_dir[] = "mero/";
	const char        *p;

	p = strstr(fname, top_src_dir);
	if (p != NULL)
		return p + strlen(top_src_dir);

	return fname;
}

#ifndef __KERNEL__
/** Resolve a hostname to a stringified IP address */
M0_INTERNAL int m0_host_resolve(const char *name, char *buf, size_t bufsiz)
{
        int            i;
        int            rc = 0;
        struct in_addr ipaddr;

        if (inet_aton(name, &ipaddr) == 0) {
                struct hostent  he;
                char            he_buf[4096];
                struct hostent *hp;
                int             herrno;

                rc = gethostbyname_r(name, &he, he_buf, sizeof he_buf,
                                     &hp, &herrno);
                if (rc != 0 || hp == NULL)
                        return -ENOENT;
                for (i = 0; hp->h_addr_list[i] != NULL; ++i)
                        /* take 1st IPv4 address found */
                        if (hp->h_addrtype == AF_INET &&
                            hp->h_length == sizeof(ipaddr))
                                break;
                if (hp->h_addr_list[i] == NULL)
                        return -EPFNOSUPPORT;
                if (inet_ntop(hp->h_addrtype, hp->h_addr, buf, bufsiz) == NULL)
                        rc = -errno;
        } else if (strlen(name) >= bufsiz) {
                rc = -ENOSPC;
        } else {
                strcpy(buf, name);
        }
        return rc;
}
#endif

M0_INTERNAL const char *m0_failed_condition;
M0_EXPORTED(m0_failed_condition);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
