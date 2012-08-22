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

#include "lib/ut.h"	/* C2_UT_ASSERT */
#include "lib/types.h"	/* c2_uint128 */

static const struct c2_uint128 zero   = C2_UINT128(0, 0);
static const struct c2_uint128 one    = C2_UINT128(0, 1);
static const struct c2_uint128 two    = C2_UINT128(0, 2);
static const struct c2_uint128 three  = C2_UINT128(0, 3);
static const struct c2_uint128 max64  = C2_UINT128(0,	       UINT64_MAX);
static const struct c2_uint128 max128 = C2_UINT128(UINT64_MAX, UINT64_MAX);

/* a + b = c */
static void uint128_add_check(const struct c2_uint128 a,
			      const struct c2_uint128 b,
			      const struct c2_uint128 c)
{
	struct c2_uint128 result;

	c2_uint128_add(&result, a, b);
	C2_UT_ASSERT(c2_uint128_eq(&result, &c));
}

static void uint128_add_check1(const struct c2_uint128 a,
			       const struct c2_uint128 b,
			       const struct c2_uint128 c)
{
	uint128_add_check(a, b, c);
	uint128_add_check(b, a, c);
}

static void uint128_add_ut(void)
{
	uint128_add_check1(zero,   zero, zero);
	uint128_add_check1(zero,   one,  one);
	uint128_add_check1(one,    one,  two);
	uint128_add_check1(one,    two,  three);
	uint128_add_check1(max128, one,  zero);
	uint128_add_check1(max128, one,  zero);
	uint128_add_check1(max128, one,  zero);
	uint128_add_check1(max128, two,  one);
}

/* a * b = c */
static void uint128_mul_check(uint64_t a,
			      uint64_t b,
			      const struct c2_uint128 *c)
{
	struct c2_uint128 result;

	c2_uint128_mul(&result, a, b);
	C2_UT_ASSERT(c2_uint128_eq(&result, c));
}

static void uint128_mul_check1(uint64_t a,
			       uint64_t b,
			       const struct c2_uint128 *c)
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
	uint128_mul_check1(2, UINT64_MAX, &C2_UINT128(1, UINT64_MAX - 1));
	uint128_mul_check1(3, UINT64_MAX, &C2_UINT128(2, UINT64_MAX - 2));
	uint128_mul_check1(UINT64_MAX, UINT64_MAX,
			   &C2_UINT128(UINT64_MAX - 1, 1));
	uint128_mul_check1(UINT32_MAX + 1ul, UINT32_MAX + 1ul,
			   &C2_UINT128(1, 0));
	uint128_mul_check1(UINT32_MAX + 1ul, UINT64_MAX,
			   &C2_UINT128((1ul << 32) - 1,
				       ((1ul << 32) - 1) << 32));
	uint128_mul_check1(UINT32_MAX + 2ul, UINT32_MAX, &max64);
}

void test_misc(void)
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
