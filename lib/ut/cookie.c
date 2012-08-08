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

#include "lib/types.h"
#include "lib/errno.h"  /* -EPROTO */
#include "lib/cookie.h"
#include "lib/ut.h"
#include "lib/memory.h" /* C2_ALLOC_PTR, C2_ALLOC_ARR */

struct dummy_struct {
	uint64_t *ds_val;
};

void test_invalid_addresses(struct c2_cookie *cookie);

void test_cookie(void)
{
	int		     flag = -1;
	uint64_t	    *addr_dummy = NULL;
	struct c2_cookie     cookie_test;
	struct dummy_struct *dummy_test;
	struct dummy_struct *dummy_retrvd = NULL;
	char		    *fake_ptr;

	C2_ALLOC_PTR(dummy_test);
	C2_UT_ASSERT(dummy_test != NULL);
	C2_ALLOC_ARR(dummy_test->ds_val, 2);
	C2_UT_ASSERT(dummy_test_ds_val != NULL);
	cookie_test.co_addr = 0;
	cookie_test.co_generation = 0;
	dummy_test->ds_val[0] = 0;
	dummy_test->ds_val[1] = 0;




	c2_free(dummy_test->ds_val);
	c2_free(dummy_test);

}
C2_EXPORTED(test_cookie);

void test_init_apis(struct c2_cookie *cookie_test, struct dummy_struct*
		    dummy_test)
{
	c2_cookie_new(dummy_test->ds_val);
	C2_UT_ASSERT(dummy_test->ds_val[0] != 0);
	c2_cookie_init(&cookie_test, dummy_test->ds_val);
	C2_UT_ASSERT(cookie_test.co_addr ==
			(uint64_t)(dummy_test->ds_val));
	C2_UT_ASSERT(cookie_test.co_generation == dummy_test->ds_val[0]);
}

void test_valid_address(struct c2_cookie *cookie_test, struct dummy_struct*
			dummy_test)
{
	uint64_t *addr_dummy = NULL;

	/*Testing c2_cookie_dereference for a valid cookie.*/
	flag = c2_cookie_dereference(cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == 0);
	C2_UT_ASSERT(addr_dummy == dummy_test->ds_val);

	/*Testing of the macro c2_cookie_of(...). for a valid cookie*/
	dummy_retrvd = c2_cookie_of(cookie_test, struct dummy_struct, ds_val);
	C2_UT_ASSERT(dummy_retrvd == dummy_test);

}

void test_invalid_address(struct c2_cookie *cookie_test, struct dummy_struct*
		          dummy_test)
{
	uint64_t	    *addr_dummy = NULL;
	int		     flag;  
	uint64_t	    *fake_ptr;

	/*Testing c2_cookie_dereference when address in a cookie is a
	 * NULL pointer.*/
	*cookie_test.co_addr = (uint64_t)NULL;
	flag = c2_cookie_dereference(cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == -EPROTO);

	/*Testing c2_cookie_dereference when address in a cookie is not greater
	 * than 4096.
	 */
	*cookie_test.co_addr = 4093;
	flag = c2_cookie_dereference(cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == -EPROTO);

	/*Testing c2_cookie_dereference when address in a cookie is
	 * not aligned to 8-bytes
	 */
	fake_ptr = (char *)(dummy_test->ds_val);
	++fake_ptr;
	cookie_test.co_addr = (uint64_t)fake_ptr;
	C2_UT_ASSERT((uint64_t *)fake_ptr > (uint64_t *)4096);
	flag = c2_cookie_dereference(&cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == -EPROTO);

	/*Testing c2_cookie_dereference for a stale cookie.*/
	c2_cookie_new(dummy_test->ds_val);
	C2_UT_ASSERT(dummy_test->ds_val[0] - cookie_test.co_generation ==
			1);
	flag = c2_cookie_dereference(&cookie_test, &addr_dummy);
	C2_UT_ASSERT(flag == -EPROTO);
}

void test_c2_cookie_of(struct c2_cookie *cookie_test)
{

	struct dummy_struct *dummy_retrvd = NULL;
	uint64_t	    *addr_dummy = NULL;
	uint64_t	    *fake_ptr;

	/*Testing c2_cookie_of when address in a cookie is a
	 * NULL pointer.*/
	*cookie_test.co_addr = (uint64_t)NULL;
	dummy_retrvd = c2_cookie_of(cookie_test, struct dummy_struct, ds_val);
	C2_UT_ASSERT(dummy_retrvd == NULL);

	/*Testing c2_cookie_of when address in a cookie is not greater
	 * than 4096.
	 */
	*cookie_test.co_addr = 4093;
	dummy_retrvd = c2_cookie_of(cookie_test, struct dummy_struct, ds_val);
	C2_UT_ASSERT(dummy_retrvd == NULL);

	/*Testing c2_cookie_of when address in a cookie is
	 * not aligned to 8-bytes. By allocating 2 locations to ds_val, and
	 * apriory ensuring that fake_ptr is greater than 4096, we are making
	 * sure that failure in the following usage of c2_cookie_of, is only due
	 * to a misaligned address.
	 */
	fake_ptr = (char *)(dummy_test->ds_val);
	++fake_ptr;
	cookie_test.co_addr = (uint64_t)fake_ptr;
	C2_UT_ASSERT((uint64_t *)fake_ptr > (uint64_t *)4096);
	dummy_retrvd = c2_cookie_of(cookie_test, struct dummy_struct, ds_val);
	C2_UT_ASSERT(dummy_retrvd == NULL);

	/*Testing c2_cookie_of for a stale cookie.*/
	c2_cookie_new(dummy_test->ds_val);
	C2_UT_ASSERT(dummy_test->ds_val[0] - cookie_test.co_generation ==
			1);
	dummy_retrvd = c2_cookie_of(cookie_test, struct dummy_struct, ds_val);
	C2_UT_ASSERT(dummy_retrvd == NULL);

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
