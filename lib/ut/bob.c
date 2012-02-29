/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 09/09/2010
 */

#include "lib/ub.h"
#include "lib/ut.h"
#include "lib/tlist.h"
#include "lib/bob.h"

enum {
	N = 256
};

struct foo {
	void           *f_payload;
	struct c2_tlink f_linkage;
	char            f_x[7];
	uint64_t        f_magix;
};

enum {
	magix = 0xbeda551edcaca0ffULL
};

C2_TL_DESCR_DEFINE(foo, "foo-s", static, struct foo, f_linkage,
		   f_magix, magix, 0);
C2_TL_DEFINE(foo, static, struct foo);

static struct c2_bob_type foo_bob;
static struct foo F;
static struct foo rank[N];

C2_BOB_DEFINE(static, &foo_bob, foo);

static void test_tlist_init(void)
{
	c2_bob_type_tlist_init(&foo_bob, &foo_tl);
	C2_UT_ASSERT(!strcmp(foo_bob.bt_name, foo_tl.td_name));
	C2_UT_ASSERT(foo_bob.bt_magix == magix);
	C2_UT_ASSERT(foo_bob.bt_magix_offset == foo_tl.td_link_magic_offset);
}

static void test_bob_init(void)
{
	foo_bob_init(&F);
	C2_UT_ASSERT(F.f_magix == magix);
	C2_UT_ASSERT(foo_bob_check(&F));
}

static void test_bob_fini(void)
{
	foo_bob_fini(&F);
	C2_UT_ASSERT(F.f_magix == 0);
	C2_UT_ASSERT(!foo_bob_check(&F));
}

static void test_tlink_init(void)
{
	foo_tlink_init(&F);
	C2_UT_ASSERT(foo_bob_check(&F));
}

static void test_tlink_fini(void)
{
	foo_tlink_fini(&F);
	C2_UT_ASSERT(foo_bob_check(&F));
	F.f_magix = 0;
	C2_UT_ASSERT(!foo_bob_check(&F));
}

static bool foo_check(const void *bob)
{
	const struct foo *f = bob;

	return f->f_payload == f + 1;
}

static void test_check(void)
{
	int i;

	foo_bob.bt_check = &foo_check;

	for (i = 0; i < N; ++i) {
		foo_bob_init(&rank[i]);
		rank[i].f_payload = rank + i + 1;
	}

	for (i = 0; i < N; ++i)
		C2_UT_ASSERT(foo_bob_check(&rank[i]));

	for (i = 0; i < N; ++i)
		foo_bob_fini(&rank[i]);

	for (i = 0; i < N; ++i)
		C2_UT_ASSERT(!foo_bob_check(&rank[i]));

}

void test_bob(void)
{
	test_tlist_init();
	test_bob_init();
	test_bob_fini();
	test_tlink_init();
	test_tlink_fini();
	test_check();
}
C2_EXPORTED(test_bob);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
