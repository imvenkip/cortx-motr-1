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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 08/20/2012
 */

#include "lib/ut.h"
#include "lib/buf.h"
#include "lib/assert.h"
#include "lib/cdefs.h"
#include "lib/memory.h"

static bool bit_is_set(int bits, int index)
{
	return (bool)(bits & (1 << index));
}

void c2_ut_lib_buf_test(void)
{
	struct c2_buf copy = C2_BUF_INIT0;
	static int d0[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
	static char *d1 = "1234567890";
	static char *d2 = "123";
	bool equal;
	int k;
	int j;
	int rc;

	struct /* test */ {
		int equality_mask; /* equality to self is implied */
		struct c2_buf buf;
	} test[] = {
		[0] = { (1 << 1), C2_BUF_INIT(strlen(d1), d1) },
	        [1] = { (1 << 0), C2_BUF_INITS(d1) },
		[2] = { (1 << 4), C2_BUF_INITS(d2) },
		[3] = { (1 << 6) | (1 << 7), C2_BUF_INIT(sizeof(d0), d0) },
		[4] = { (1 << 2), C2_BUF_INIT(strlen(d2), d1) },
		[5] = { 0, C2_BUF_INIT(sizeof(d0) - 1, d0) },

		/* [6] and [7] are placeholders and will be overwriten with
		 * c2_buf_init() */
		[6] = { (1 << 3) | (1 << 7), C2_BUF_INIT0 },
		[7] = { (1 << 3) | (1 << 6), C2_BUF_INIT0 },
	};

	c2_buf_init(&test[6].buf, d0, sizeof(d0));
	c2_buf_init(&test[7].buf, d0, sizeof(d0));

	for (k = 0; k < ARRAY_SIZE(test); ++k) {
		rc = c2_buf_copy(&copy, &test[k].buf);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(c2_buf_eq(&copy, &test[k].buf));

		c2_buf_free(&copy);
	}

	for (k = 0; k < ARRAY_SIZE(test); ++k) {
		for (j = 0; j < ARRAY_SIZE(test); ++j) {
			if (j == k)
				continue;

			equal = c2_buf_eq(&test[j].buf, &test[k].buf);
			C2_UT_ASSERT(equal == bit_is_set(test[j].equality_mask,
							 k));
		}
	}
}
C2_EXPORTED(c2_ut_lib_buf_test);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
