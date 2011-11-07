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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 11/05/2011
 */

#include "lib/ut.h"
#include "lib/aptr.h"
#include "lib/assert.h"
#include "lib/thread.h"

#include <stdlib.h>	/* rand() */

enum {
	NR = 16,
	NR_THREADS = 16,
	NR_CAS_PER_THREAD = 100000
};

static void *data[NR_THREADS];
static struct c2_aptr aptr;

static void fill_rand(void *data, int size)
{
	int i;
	char *cdata = data;

	C2_ASSERT(cdata != NULL);
	for (i = 0; i < size; ++i)
		cdata[i] = rand() & 0xFF;
}

static void test_aptr_init()
{
	struct c2_aptr ap;

	fill_rand(&ap, sizeof ap);
	c2_aptr_init(&ap);
	C2_UT_ASSERT(c2_aptr_ptr(&ap) == NULL);
	C2_UT_ASSERT(c2_aptr_count(&ap) == 0);
	c2_aptr_fini(&ap);
}

static void test_aptr_getset()
{
	struct c2_aptr ap;
	struct c2_aptr ap1;

	c2_aptr_init(&ap);
	c2_aptr_init(&ap1);
	
	fill_rand(&ap, sizeof ap);
	C2_UT_ASSERT(c2_aptr_ptr(&ap) == c2_aptr_ptr(&ap));
	C2_UT_ASSERT(c2_aptr_count(&ap) == c2_aptr_count(&ap));

	c2_aptr_set(&ap1, c2_aptr_ptr(&ap), c2_aptr_count(&ap));
	C2_UT_ASSERT(c2_aptr_ptr(&ap) == c2_aptr_ptr(&ap1));
	C2_UT_ASSERT(c2_aptr_count(&ap) == c2_aptr_count(&ap1));

	c2_aptr_set(&ap1, (char *) c2_aptr_ptr(&ap) + 1, c2_aptr_count(&ap) + 1);
	C2_UT_ASSERT(c2_aptr_ptr(&ap) != c2_aptr_ptr(&ap1));
	C2_UT_ASSERT(c2_aptr_count(&ap) != c2_aptr_count(&ap1));

	c2_aptr_set(&ap, NULL, 0);
	C2_UT_ASSERT(c2_aptr_ptr(&ap) == NULL);
	C2_UT_ASSERT(c2_aptr_count(&ap) == 0);

	c2_aptr_fini(&ap);
	c2_aptr_fini(&ap1);
}

static void test_aptr_copy()
{
	struct c2_aptr ap;
	struct c2_aptr ap1;

	c2_aptr_init(&ap);
	c2_aptr_init(&ap1);

	fill_rand(&ap, sizeof ap);
	c2_aptr_set(&ap1, (char *) c2_aptr_ptr(&ap) + 1, c2_aptr_count(&ap) + 1);
	C2_UT_ASSERT(c2_aptr_ptr(&ap) != c2_aptr_ptr(&ap1));
	C2_UT_ASSERT(c2_aptr_count(&ap) != c2_aptr_count(&ap1));

	c2_aptr_copy(&ap1, &ap);
	C2_UT_ASSERT(c2_aptr_ptr(&ap) == c2_aptr_ptr(&ap1));
	C2_UT_ASSERT(c2_aptr_count(&ap) == c2_aptr_count(&ap1));

	c2_aptr_fini(&ap);
	c2_aptr_fini(&ap1);
}

static void test_aptr_eq()
{
	struct c2_aptr ap;
	struct c2_aptr ap1;

	c2_aptr_init(&ap);
	c2_aptr_init(&ap1);
	C2_UT_ASSERT(c2_aptr_eq(&ap1, &ap));

	fill_rand(&ap, sizeof ap);
	c2_aptr_set(&ap1, (char *) c2_aptr_ptr(&ap) + 1, c2_aptr_count(&ap) + 1);
	C2_UT_ASSERT(!c2_aptr_eq(&ap1, &ap));

	c2_aptr_set(&ap1, c2_aptr_ptr(&ap), c2_aptr_count(&ap) + 1);
	C2_UT_ASSERT(!c2_aptr_eq(&ap1, &ap));

	c2_aptr_set(&ap1, (char *) c2_aptr_ptr(&ap) + 1, c2_aptr_count(&ap));
	C2_UT_ASSERT(!c2_aptr_eq(&ap1, &ap));

	c2_aptr_copy(&ap1, &ap);
	C2_UT_ASSERT(c2_aptr_eq(&ap1, &ap));

	c2_aptr_fini(&ap);
	c2_aptr_fini(&ap1);
}

static void worker(void *data)
{
	int i = 0;
	int count;
	struct c2_aptr old_p;
	struct c2_aptr new_p;

	c2_aptr_init(&old_p);
	c2_aptr_init(&new_p);

	while (i < NR_CAS_PER_THREAD) {
		c2_aptr_copy(&old_p, &aptr);
		count = c2_aptr_count(&old_p);
		if (c2_aptr_cas(&aptr, &old_p, data, count + 1))
			++i;
	}

	c2_aptr_fini(&old_p);
	c2_aptr_fini(&new_p);
}

static void test_aptr_cas()
{
	struct c2_aptr ap;
	struct c2_aptr ap1;

	c2_aptr_init(&ap);
	c2_aptr_init(&ap1);

	c2_aptr_set(&ap, NULL, 1);
	c2_aptr_set(&ap1, &ap1, 2);

	C2_UT_ASSERT(!c2_aptr_cas(&ap, &ap1, &ap, 4));
	C2_UT_ASSERT(c2_aptr_ptr(&ap) == NULL);
	C2_UT_ASSERT(c2_aptr_count(&ap) == 1);

	c2_aptr_set(&ap1, NULL, 1);
	C2_UT_ASSERT(c2_aptr_cas(&ap, &ap1, &ap, 2));
	C2_UT_ASSERT(c2_aptr_ptr(&ap) == &ap);
	C2_UT_ASSERT(c2_aptr_count(&ap) == 2);

	c2_aptr_fini(&ap);
	c2_aptr_fini(&ap1);
}

static void test_aptr_cas_mt()
{
	static struct c2_thread threads[NR_THREADS];
	int i;
	int rc;

	fill_rand(data, sizeof data);
	c2_aptr_init(&aptr);

	for (i = 0; i < NR_THREADS; ++i) {
		rc = C2_THREAD_INIT(&threads[i], void *, NULL,
				&worker, data[i], "aptr_worker");
		C2_ASSERT(rc == 0);
	}
	for (i = 0; i < NR_THREADS; ++i) {
		c2_thread_join(&threads[i]);
		c2_thread_fini(&threads[i]);
	}

	C2_UT_ASSERT(c2_aptr_count(&aptr) == NR_THREADS * NR_CAS_PER_THREAD);

	rc = 1;
	for (i = 0; i < NR_THREADS; ++i)
		if (c2_aptr_ptr(&aptr) == data[i])
			rc = 0;
	C2_UT_ASSERT(rc == 0);

	c2_aptr_fini(&aptr);
}

void test_aptr(void)
{
	int i;

	for (i = 0; i < NR; ++i) {
		srand(i);
		test_aptr_init();
		test_aptr_getset();
		test_aptr_copy();
		test_aptr_eq();
		test_aptr_cas();
		test_aptr_cas_mt();
	}
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
