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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 08/22/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/ut.h"	/* M0_UT_ASSERT */
#include "lib/types.h"	/* m0_uint128 */

static const struct m0_uint128 zero    = M0_UINT128(0, 0);
static const struct m0_uint128 one     = M0_UINT128(0, 1);
static const struct m0_uint128 two     = M0_UINT128(0, 2);
static const struct m0_uint128 three   = M0_UINT128(0, 3);
static const struct m0_uint128 max64   = M0_UINT128(0,		UINT64_MAX);
static const struct m0_uint128 max64_1 = M0_UINT128(1,	        0);
static const struct m0_uint128 max64_2 = M0_UINT128(1,	        1);
static const struct m0_uint128 max128  = M0_UINT128(UINT64_MAX, UINT64_MAX);

/* a + b = c */
static void uint128_add_check(const struct m0_uint128 a,
			      const struct m0_uint128 b,
			      const struct m0_uint128 c)
{
	struct m0_uint128 result;

	m0_uint128_add(&result, a, b);
	M0_UT_ASSERT(m0_uint128_eq(&result, &c));
}

static void uint128_add_check1(const struct m0_uint128 a,
			       const struct m0_uint128 b,
			       const struct m0_uint128 c)
{
	uint128_add_check(a, b, c);
	uint128_add_check(b, a, c);
}

static void uint128_add_ut(void)
{
	uint128_add_check1(zero,   zero,    zero);
	uint128_add_check1(zero,   one,     one);
	uint128_add_check1(one,    one,     two);
	uint128_add_check1(one,    two,     three);
	uint128_add_check1(max64,  zero,    max64);
	uint128_add_check1(max64,  one,	    max64_1);
	uint128_add_check1(max64,  two,     max64_2);
	uint128_add_check1(max128, one,     zero);
	uint128_add_check1(max128, two,     one);
	uint128_add_check1(max128, three,   two);
	uint128_add_check1(max128, max64_1, max64);
	uint128_add_check1(max128, max64_2, max64_1);
}

/* a * b = c */
static void uint128_mul_check(uint64_t a,
			      uint64_t b,
			      const struct m0_uint128 *c)
{
	struct m0_uint128 result;

	m0_uint128_mul64(&result, a, b);
	M0_UT_ASSERT(m0_uint128_eq(&result, c));
}

static void uint128_mul_check1(uint64_t a,
			       uint64_t b,
			       const struct m0_uint128 *c)
{
	uint128_mul_check(a, b, c);
	uint128_mul_check(b, a, c);
}

static void uint128_mul_ut(void)
{
	uint128_mul_check1(0, 0, &zero);
	uint128_mul_check1(0, 1, &zero);
	uint128_mul_check1(0, UINT64_MAX, &zero);
	uint128_mul_check1(1, 1, &one);
	uint128_mul_check1(1, 2, &two);
	uint128_mul_check1(1, UINT64_MAX, &max64);
	uint128_mul_check1(2, UINT64_MAX, &M0_UINT128(1, UINT64_MAX - 1));
	uint128_mul_check1(3, UINT64_MAX, &M0_UINT128(2, UINT64_MAX - 2));
	uint128_mul_check1(UINT64_MAX, UINT64_MAX,
			   &M0_UINT128(UINT64_MAX - 1, 1));
	uint128_mul_check1(UINT32_MAX + 1ul, UINT32_MAX + 1ul, &max64_1);
	uint128_mul_check1(UINT32_MAX + 1ul, UINT64_MAX,
			   &M0_UINT128(UINT32_MAX,
				       (uint64_t) UINT32_MAX << 32));
	uint128_mul_check1(UINT32_MAX + 2ul, UINT32_MAX, &max64);
}

void m0_test_misc(void)
{
	uint128_add_ut();
	uint128_mul_ut();
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
