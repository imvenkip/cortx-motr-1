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
 * Original creation date: 07/09/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/** @todo create lib/limits.h */
#ifndef __KERNEL__
#include <limits.h>		/* ULONG_MAX */
#else
#include <linux/kernel.h>	/* ULONG_MAX */
#endif

#include "lib/assert.h"		/* C2_PRE */
#include "lib/cdefs.h"		/* NULL */

#include "net/test/uint256.h"

/**
   @defgroup NetTestInt256Internals Statistics Collector
   @ingroup NetTestInternals

   @see
   @ref net-test

   @{
 */

void c2_net_test_uint256_set(struct c2_net_test_uint256 *a,
			     unsigned long value)
{
	int i;

	C2_PRE(a != NULL);

	c2_net_test_uint256_qword_set(a, 0, value);
	for (i = 1; i < C2_NET_TEST_UINT256_QWORDS_NR; ++i)
		c2_net_test_uint256_qword_set(a, i, 0);
}

void c2_net_test_uint256_add(struct c2_net_test_uint256 *a,
			     unsigned long value)
{
	struct c2_net_test_uint256 b;

	c2_net_test_uint256_set(&b, value);
	c2_net_test_uint256_add_uint256(a, &b);
}

void c2_net_test_uint256_add_sqr(struct c2_net_test_uint256 *a,
			         unsigned long value)
{
	struct c2_net_test_uint256 part_sqr;
	struct c2_net_test_uint256 part_double;
	unsigned long		   v0;
	unsigned long		   v1;
	unsigned long		   half_ul;

	C2_PRE(a != NULL);

	C2_CASSERT(C2_NET_TEST_UINT256_QWORDS_NR > 1);
	C2_CASSERT((CHAR_BIT * sizeof (unsigned long) % 2) == 0);

	half_ul = 1ul << CHAR_BIT * sizeof (unsigned long) / 2;
	c2_net_test_uint256_set(&part_sqr,    0);
	c2_net_test_uint256_set(&part_double, 0);

	/* value = v0 + v1 * half_ul */
	v0 = value % half_ul;
	v1 = value / half_ul;
	/*
	   part_sqr    = v0 * v0 + v1 * v1 * half_ul * half_ul
	   part_double = 2 * v0 * v1 * half_ul
	   value * value = part_sqr + part_double
	 */
	c2_net_test_uint256_qword_set(&part_sqr,    0,	    v0 * v0);
	c2_net_test_uint256_qword_set(&part_sqr,    1,	    v1 * v1);
	c2_net_test_uint256_qword_set(&part_double, 0, (2 * v1 * v0 % half_ul) *
				      half_ul);
	c2_net_test_uint256_qword_set(&part_double, 1,  2 * v1 * v1 / half_ul);

	c2_net_test_uint256_add_uint256(a, &part_sqr);
	c2_net_test_uint256_add_uint256(a, &part_double);
}

void
c2_net_test_uint256_add_uint256(struct c2_net_test_uint256 *a,
				const struct c2_net_test_uint256 *value256)
{
	unsigned long qa;
	unsigned long qv;
	unsigned long carry = 0;
	unsigned long carry_new;
	int	      i;

	C2_PRE(a != NULL);
	C2_PRE(value256 != NULL);

	for (i = 0; i < C2_NET_TEST_UINT256_QWORDS_NR; ++i) {
		qa = c2_net_test_uint256_qword_get(a, i);
		qv = c2_net_test_uint256_qword_get(value256, i);

		carry_new = qv == ULONG_MAX ? qa == 0 ? carry : 1
					    : ULONG_MAX - qa < qv + carry;
		c2_net_test_uint256_qword_set(a, i, qa + qv + carry);
		carry = carry_new;
		C2_ASSERT(carry == 0 || carry == 1);
	}
}

unsigned long c2_net_test_uint256_qword_get(const struct c2_net_test_uint256 *a,
					    unsigned index)
{
	C2_PRE(a != NULL);
	C2_PRE(index < C2_NET_TEST_UINT256_QWORDS_NR);

	return a->ntsi_qword[index];
}

void c2_net_test_uint256_qword_set(struct c2_net_test_uint256 *a,
				   unsigned index,
				   unsigned long value)
{
	C2_PRE(a != NULL);
	C2_PRE(index < C2_NET_TEST_UINT256_QWORDS_NR);

	a->ntsi_qword[index] = value;
}

bool c2_net_test_uint256_is_eq(const struct c2_net_test_uint256 *a,
			       unsigned long value)
{
	int i;

	if (c2_net_test_uint256_qword_get(a, 0) != value)
		return false;
	for (i = 1; i < C2_NET_TEST_UINT256_QWORDS_NR; ++i)
		if (c2_net_test_uint256_qword_get(a, i) != 0)
			return false;
	return true;
}

#ifndef __KERNEL__
double c2_net_test_uint256_double_get(const struct c2_net_test_uint256 *a)
{
	const double base   = 1. + ULONG_MAX;
	double	     pow    = 1.;
	double	     result = .0;
	int	     i;

	C2_PRE(a != NULL);

	for (i = 0; i < C2_NET_TEST_UINT256_QWORDS_NR; ++i) {
		result += pow * c2_net_test_uint256_qword_get(a, i);
		pow *= base;
	}
	return result;
}
#endif

c2_bcount_t c2_net_test_uint256_serialize(enum c2_net_test_serialize_op op,
					  struct c2_net_test_uint256 *a,
					  struct c2_bufvec *bv,
					  c2_bcount_t bv_offset)
{
	unsigned long ul;
	c2_bcount_t   len_total = 0;
	c2_bcount_t   len	= 0;
	int	      i;

	C2_PRE(ergo(op == C2_NET_TEST_DESERIALIZE, a != NULL));

	for (i = 0; i < C2_NET_TEST_UINT256_QWORDS_NR; ++i) {
		if (op == C2_NET_TEST_SERIALIZE)
			ul = a == NULL ? 0 :
					 c2_net_test_uint256_qword_get(a, i);

		len = c2_net_test_serialize_data(op, &ul, sizeof ul, false,
						 bv, bv_offset + len_total);
		if (len == 0)
			break;
		len_total += len;

		if (op == C2_NET_TEST_DESERIALIZE)
			c2_net_test_uint256_qword_set(a, i, ul);
	}
	return len == 0 ? 0 : len_total;
}

/**
   @} end of NetTestInt256Internals group
 */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
