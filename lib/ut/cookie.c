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
 * Original author: Nachiket Sahasrabudhe <Nachiket_Sahasrabudhe@xyratex.com>
 * Original creation date: 30/07/2012
 */

/**
 * This file has 6 test-cases as enumerated below:
 * Test No.0: Test for a valid cookie.
 * Test No.1: Test for a cookie holding a NULL pointer.
 * Test No.2: Test for a cookie with address less than 4096.
 * Test No.3: Test for a cookie with unaligned address.
 * Test No.4: Test for a stale cookie.
 * Test No.5: Test for APIs related to cookie-initialization.
 * Function c2_cookie_dereference is tested for these five cases. The macro
 * c2_cookie_of, is tested for Test No.0 and Test No.1. For the macro
 * c2_cookie_of, test-cases 1 to 4 are equivalent. The function c2_addr_is_sane
 * gets implicitly tested with the function c2_cookie_dereference. Test No.5
 * tests the initialization APIs.
 * */

#include "lib/types.h"
#include "lib/errno.h"  /* -EPROTO */
#include "lib/cookie.h"
#include "lib/ut.h"
#include "lib/memory.h" /* C2_ALLOC_PTR */

struct dummy_struct {
	uint64_t ds_val;
};

static void test_init_apis(struct c2_cookie *cookie_test, struct dummy_struct*
		           dummy_test)
{
	/* Test No.6: Testing of init-apis. */
	c2_cookie_new(&dummy_test->ds_val);
	C2_UT_ASSERT(dummy_test->ds_val > 0);
	c2_cookie_init(cookie_test, &dummy_test->ds_val);
	C2_UT_ASSERT(cookie_test->co_addr ==
		     (uint64_t)(&dummy_test->ds_val));
	C2_UT_ASSERT(cookie_test->co_generation == dummy_test->ds_val);
}

static void test_valid_cookie(struct c2_cookie *cookie_test,
		               struct dummy_struct* dummy_test)
{
	int		     flag;
	uint64_t	    *addr_dummy = NULL;
	struct dummy_struct *dummy_retrvd = NULL;

	/* Test No.0: Testing c2_cookie_dereference for a valid cookie. */
	flag = c2_cookie_dereference(cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == 0);
	C2_UT_ASSERT(addr_dummy == &dummy_test->ds_val);

	/* Test No.0: Testing of the macro c2_cookie_of(...) for a
	 * valid cookie. */
	dummy_retrvd = c2_cookie_of(cookie_test, struct dummy_struct, ds_val);
	C2_UT_ASSERT(dummy_retrvd == dummy_test);
}

static void test_c2_cookie_dereference(struct c2_cookie *cookie_test,
		                       struct dummy_struct *dummy_test)
{
	uint64_t *addr_dummy = NULL;
	int	  flag;
	char     *fake_ptr;

	/* Test No.1: Testing c2_cookie_dereference when address in a
	 * cookie is a NULL pointer. */
	cookie_test->co_addr = (uint64_t)NULL;
	flag = c2_cookie_dereference(cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == -EPROTO);

	/* Test No.2: Testing c2_cookie_dereference when address in a
	 * cookie is not greater than 4096. */
	cookie_test->co_addr = 4093;
	flag = c2_cookie_dereference(cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == -EPROTO);

	/* Test No.3: Testing c2_cookie_dereference when address in a
	 * cookie is not aligned to 8-bytes. */
	fake_ptr = (char *)&dummy_test->ds_val;
	fake_ptr++;
	cookie_test->co_addr = (uint64_t)fake_ptr;
	flag = c2_cookie_dereference(cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == -EPROTO);

	/* Test No.4: Testing c2_cookie_dereference for a stale cookie. */
	c2_cookie_new(&dummy_test->ds_val);
	C2_UT_ASSERT(dummy_test->ds_val - cookie_test->co_generation == 1);

	/* Restoring an address in cookie, that got tampered in the last Test.
	 */
	cookie_test->co_addr = (uint64_t)(&dummy_test->ds_val);
	flag = c2_cookie_dereference(cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == -EPROTO);
}

static void test_c2_cookie_of(struct c2_cookie *cookie_test,
		              struct dummy_struct *dummy_test)
{
	struct dummy_struct *dummy_retrvd = NULL;

	/* Test No.1: Testing c2_cookie_of when the address in a cookie is a
	 * NULL pointer. */
	cookie_test->co_addr = (uint64_t)NULL;
	dummy_retrvd = c2_cookie_of(cookie_test, struct dummy_struct, ds_val);
	C2_UT_ASSERT(dummy_retrvd == NULL);
}

void test_cookie(void)
{
	struct c2_cookie     cookie_test;
	struct dummy_struct *dummy_test;

	C2_ALLOC_PTR(dummy_test);
	C2_UT_ASSERT(dummy_test != NULL);
	cookie_test.co_addr = 0;
	cookie_test.co_generation = 0;
	dummy_test->ds_val = 0;

	test_init_apis(&cookie_test, dummy_test);
	test_valid_cookie(&cookie_test, dummy_test);

	test_init_apis(&cookie_test, dummy_test);
	test_c2_cookie_dereference(&cookie_test, dummy_test);

	test_init_apis(&cookie_test, dummy_test);
	test_c2_cookie_of(&cookie_test, dummy_test);

	c2_free(dummy_test);
}
C2_EXPORTED(test_cookie);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
