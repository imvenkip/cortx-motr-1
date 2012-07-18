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

#include "lib/ut.h"		/* C2_UT_ASSERT */
#include "lib/misc.h"		/* C2_SET0 */

#include "net/test/uint256.h"

enum {
	UINT256_ADD_NR	   = 100,
	UINT256_BUF_LEN	   = 0x100,
	UINT256_BUF_OFFSET = 42,
};

static void try_serialize(struct c2_net_test_uint256 *a)
{
	struct c2_net_test_uint256 b;
	char			   bv_buf[UINT256_BUF_LEN];
	void			  *bv_addr = bv_buf;
	c2_bcount_t		   bv_len = UINT256_BUF_LEN;
	struct c2_bufvec	   bv = C2_BUFVEC_INIT_BUF(&bv_addr, &bv_len);
	c2_bcount_t		   serialized_len;
	c2_bcount_t		   len;
	int			   i;
	unsigned long		   qa;
	unsigned long		   qb;

	serialized_len = c2_net_test_uint256_serialize(C2_NET_TEST_SERIALIZE,
						       a, &bv,
						       UINT256_BUF_OFFSET);
	C2_UT_ASSERT(serialized_len > 0);

	C2_SET0(&b);
	len = c2_net_test_uint256_serialize(C2_NET_TEST_DESERIALIZE,
					    &b, &bv, UINT256_BUF_OFFSET);
	C2_UT_ASSERT(len == serialized_len);

	for (i = 0; i < C2_NET_TEST_UINT256_QWORDS_NR; ++i) {
		qa = c2_net_test_uint256_qword_get(a, i);
		qb = c2_net_test_uint256_qword_get(&b, i);
		C2_UT_ASSERT(qa == qb);
	}
}

static void assert_qword_eq(const struct c2_net_test_uint256 *a,
			    unsigned index,
			    unsigned long value)
{
	C2_UT_ASSERT(c2_net_test_uint256_qword_get(a, index) == value);
}

static void qword_set_and_test(struct c2_net_test_uint256 *a,
			       unsigned index,
			       unsigned long value)
{
	c2_net_test_uint256_qword_set(a, index, value);
	assert_qword_eq(a, index, value);
	try_serialize(a);
}

#ifndef __KERNEL__
static void assert_double_eq(const struct c2_net_test_uint256 *a, double value)
{
	static const double eps = 1E-10;
	double		    da = c2_net_test_uint256_double_get(a);

	C2_UT_ASSERT(value * (1. - eps) <= da && da <= value * (1. + eps));
}
#endif

static void assert_eq(struct c2_net_test_uint256 *a, unsigned long value)
{
	struct c2_net_test_uint256 b;
	int			   i;

	try_serialize(a);
	C2_UT_ASSERT(c2_net_test_uint256_is_eq(a, value));
	assert_qword_eq(a, 0, value);
	for (i = 1; i < C2_NET_TEST_UINT256_QWORDS_NR; ++i)
		assert_qword_eq(a, i, 0);
#ifndef __KERNEL__
	assert_double_eq(a, value);
#endif

	C2_SET0(&b);
	c2_net_test_uint256_qword_set(&b, 0,
				      c2_net_test_uint256_qword_get(a, 0));
	C2_UT_ASSERT(c2_net_test_uint256_is_eq(&b, value));
}

static void set_and_test(struct c2_net_test_uint256 *a, unsigned long value)
{
	c2_net_test_uint256_set(a, value);
	assert_eq(a, value);
}

static void uint256_ut_double(void)
{
#ifndef __KERNEL__
	struct c2_net_test_uint256 a;
	const double		   base = 1. + ULONG_MAX;
	double			   pow  = base;
	double			   v    = 1.;
	int			   i;

	c2_net_test_uint256_set(&a, 0);
	for (i = 0; i < C2_NET_TEST_UINT256_QWORDS_NR; ++i) {
		qword_set_and_test(&a, i, 1);
		assert_double_eq(&a, v);
		try_serialize(&a);
		v += pow;
		pow *= base;
	}
#endif
}

void c2_net_test_uint256_ut(void)
{
	struct c2_net_test_uint256 a;
	struct c2_net_test_uint256 b;
	struct c2_net_test_uint256 a2;
	unsigned long		   i;
	unsigned long		   j;
	unsigned long		   result = 0;

	/* test #0: set-and-test */
	set_and_test(&a, 0);

	/* test #1: add-and-test */
	for (i = 0; i < UINT256_ADD_NR; ++i) {
		c2_net_test_uint256_add(&a, 1);
		assert_eq(&a, i + 1);
	}

	/* test #2: add-sqr-test and add-int256-test */
	set_and_test(&a, 0);
	for (i = 0; i < UINT256_ADD_NR; ++i) {
		C2_UT_ASSERT(ULONG_MAX - i * i >= result);

		a2 = a;

		result += i * i;

		c2_net_test_uint256_add_sqr(&a, i);
		assert_eq(&a, result);

		set_and_test(&b, i * i);
		c2_net_test_uint256_add_uint256(&a2, &b);
		assert_eq(&b, i * i);
		assert_eq(&a2, result);
	}

	/* test #3: add-with-carry-test */
	for (i = 1; i < UINT256_ADD_NR; ++i) {
		set_and_test(&a, ULONG_MAX);
		c2_net_test_uint256_add(&a, i);
		assert_qword_eq(&a, 0, i - 1);
		if (C2_NET_TEST_UINT256_QWORDS_NR > 1)
			assert_qword_eq(&a, 1, 1);
		for (j = 2; j < C2_NET_TEST_UINT256_QWORDS_NR; ++j)
			assert_qword_eq(&a, j, 0);
	}

	/* test #4: add-with-multiple-carry-test */
	for (i = 1; i < C2_NET_TEST_UINT256_QWORDS_NR; ++i) {
		for (j = 0; j < i; ++j)
			qword_set_and_test(&a, j, ULONG_MAX);
		for (j = i; j < C2_NET_TEST_UINT256_QWORDS_NR; ++j)
			qword_set_and_test(&a, j, 0);

		a2 = a;
		c2_net_test_uint256_set(&b, 42);
		c2_net_test_uint256_add_uint256(&a, &b);
		assert_qword_eq(&a, 0, 41);
		for (j = 1; j < i; ++j)
			assert_qword_eq(&a, j, 0);
		for (j = i; j < C2_NET_TEST_UINT256_QWORDS_NR; ++j)
			assert_qword_eq(&a, j, j == i);

		b = a2;
		c2_net_test_uint256_add_uint256(&b, &a2);
		assert_qword_eq(&b, 0, ULONG_MAX - 1);
		for (j = 1; j < i; ++j)
			assert_qword_eq(&b, j, ULONG_MAX);
		for (j = i; j < C2_NET_TEST_UINT256_QWORDS_NR; ++j)
			assert_qword_eq(&a, j, j == i);
	}

	uint256_ut_double();
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
