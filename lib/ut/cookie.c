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
 * This file has seven test-cases in all. Out of these seven cases,
 * six cases deal with cookie-APIs, and one case tests the function
 * c2_addr_is_sane.
 * The six cases related to cookie-APIs are as below:
 * Test No.0: Test for a valid cookie.
 * Test No.1: Test for a cookie holding a NULL pointer.
 * Test No.2: Test for a cookie with address less than 4096.
 * Test No.3: Test for a cookie with unaligned address.
 * Test No.4: Test for a stale cookie.
 * Test No.5: Test for APIs related to cookie-initialization.
 * */

#ifndef __KERNEL__
#include <unistd.h> /* sbrk(0) */
#endif
#include "lib/types.h"
#include "lib/errno.h" /* -EPROTO */
#include "lib/cookie.h"
#include "lib/ut.h"
#include "lib/memory.h" /* C2_ALLOC_PTR */
#include "lib/arith.h" /* C2_IS_8ALIGNED */

struct obj_struct {
	uint64_t os_val;
};

static uint64_t          bss;
static const uint64_t    readonly = 2;
static struct obj_struct obj_bss;

static void test_init_apis(struct c2_cookie *cookie_test, struct obj_struct*
		           obj)
{
	/* Test No.6: Testing of init-apis. */
	c2_cookie_new(&obj->os_val);
	C2_UT_ASSERT(obj->os_val > 0);
	c2_cookie_init(cookie_test, &obj->os_val);
	C2_UT_ASSERT(cookie_test->co_addr ==
		     (uint64_t)&obj->os_val);
	C2_UT_ASSERT(cookie_test->co_generation == obj->os_val);
}

static void test_valid_cookie(struct c2_cookie *cookie_test,
		              struct obj_struct* obj)
{
	int		   flag;
	uint64_t	  *addr_dummy;
	struct obj_struct *obj_retrvd ;

	/* Test No.0: Testing c2_cookie_dereference for a valid cookie. */
	flag = c2_cookie_dereference(cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == 0);
	C2_UT_ASSERT(addr_dummy == &obj->os_val);

	/* Test No.0: Testing of the macro c2_cookie_of(...) for a
	 * valid cookie. */
	obj_retrvd = c2_cookie_of(cookie_test, struct obj_struct, os_val);
	C2_UT_ASSERT(obj_retrvd == obj);
}

static void test_c2_cookie_dereference(struct c2_cookie *cookie_test,
		                       struct obj_struct *obj)
{
	uint64_t *addr_dummy;
	int	  flag;
	char     *fake_ptr;

	/* Test No.1: Testing c2_cookie_dereference when address in a
	 * cookie is a NULL pointer. */
	cookie_test->co_addr = (uint64_t)NULL;
	flag = c2_cookie_dereference(cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == -EPROTO);

	/* Test No.2: Testing c2_cookie_dereference when address in a
	 * cookie is not greater than 4096. */
	cookie_test->co_addr = 2048;
	flag = c2_cookie_dereference(cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == -EPROTO);

	/* Test No.3: Testing c2_cookie_dereference when address in a
	 * cookie is not aligned to 8-bytes. */
	fake_ptr = (char *)&obj->os_val;
	fake_ptr++;
	cookie_test->co_addr = (uint64_t)fake_ptr;
	flag = c2_cookie_dereference(cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == -EPROTO);

	/* Test No.4: Testing c2_cookie_dereference for a stale cookie. */
	c2_cookie_new(&obj->os_val);
	C2_UT_ASSERT(obj->os_val != cookie_test->co_generation);

	/* Restoring an address in cookie, that got tampered in the last Test.
	 */
	cookie_test->co_addr = (uint64_t)(&obj->os_val);
	flag = c2_cookie_dereference(cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == -EPROTO);
}

static void test_c2_cookie_of(struct c2_cookie *cookie_test,
		              struct obj_struct *obj)
{
	struct obj_struct *obj_retrvd;

	/* Test No.1: Testing c2_cookie_of when address in a cookie is a
	 * NULL pointer. */
	cookie_test->co_addr = (uint64_t)NULL;
	obj_retrvd = c2_cookie_of(cookie_test, struct obj_struct, os_val);
	C2_UT_ASSERT(obj_retrvd == NULL);
}

static void addr_sanity(const uint64_t *addr, bool sane)
{
		C2_UT_ASSERT(c2_addr_is_sane(addr) == sane);
}

void test_cookie(void)
{
	uint64_t	   automatic;
	uint64_t	  *dynamic;
	uint64_t	   i;
	bool		   insane;
	struct c2_cookie   cookie_test;
	struct obj_struct *obj_dynamic;
	struct obj_struct  obj_automatic;
	struct obj_struct *obj_ptrs[3];

	C2_ALLOC_PTR(dynamic);
	C2_UT_ASSERT(dynamic != NULL);

	/* Address-sanity testing */
	addr_sanity(NULL, false);
	addr_sanity((uint64_t*)1, false);
	addr_sanity((uint64_t*)8, false);
	addr_sanity(&automatic, true);
	addr_sanity(dynamic, true);
	addr_sanity(&bss, true);
	addr_sanity(&readonly, true);

	if (C2_IS_8ALIGNED(&test_cookie))
		addr_sanity((uint64_t *)&test_cookie, true);
	else
		addr_sanity((uint64_t *)&test_cookie, false);

	c2_free(dynamic);

	/*
	 * run through address space, checking that c2_addr_is_sane() doesn't
	 * crash.
	 */
	for (i = 1, insane = false; i <= 0xffff; i++) {
		uint64_t word;
		void    *addr;
		bool     sane;

		word = (i & ~0xf) | (i << 16) | (i << 32) | (i << 48);
		addr = (uint64_t *)word;
		sane = c2_addr_is_sane(addr);
#ifndef __KERNEL__
		C2_UT_ASSERT(ergo(addr < sbrk(0), sane));
#endif
		insane |= !sane;
	}

	/* check that at least one really invalid address was tested. */
	C2_UT_ASSERT(insane);

	/*Testing cookie-APIs*/
	C2_ALLOC_PTR(obj_dynamic);
	C2_UT_ASSERT(obj_dynamic != NULL);

	obj_ptrs[0] = obj_dynamic;
	obj_ptrs[1] = &obj_automatic;
	obj_ptrs[2] = &obj_bss;

	for (i = 0; i < 3; ++i) {
		test_init_apis(&cookie_test, obj_ptrs[i]);
		test_valid_cookie(&cookie_test, obj_ptrs[i]);
		test_init_apis(&cookie_test, obj_ptrs[i]);
		test_c2_cookie_dereference(&cookie_test, obj_ptrs[i]);
		test_init_apis(&cookie_test, obj_ptrs[i]);
		test_c2_cookie_of(&cookie_test, obj_ptrs[i]);
	}

	c2_free(obj_dynamic);
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
