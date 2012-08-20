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

void test_buf(void)
{
	struct c2_buf copy = C2_BUF_INIT0;
	int i[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
	char *s = "1234567890";
	char *t = "123";
	bool equal;
	int k;
	int j;
	int rc;

	struct /* test */ {
		int equality_mask;
		struct c2_buf buf;
	} test[] = {
		[0] = { (1 << 0) | (1 << 1), C2_BUF_INIT(strlen(s), s) },
	        [1] = { (1 << 1) | (1 << 0), C2_BUF_INITS(s) },
		[2] = { (1 << 2) | (1 << 4), C2_BUF_INITS(t) },

		[3] = { (1 << 3) | (1 << 6) | (1 << 7),
			C2_BUF_INIT(ARRAY_SIZE(i) * sizeof(i), i) },

		[4] = { (1 << 4) | (1 << 2), C2_BUF_INIT(strlen(t), s) },

		[5] = { (1 << 5),
			C2_BUF_INIT((ARRAY_SIZE(i) - 1) * sizeof(i), i) },

		[6] = { (1 << 6) | (1 << 3) | (1 << 7), C2_BUF_INIT0 },
		[7] = { (1 << 7) | (1 << 3) | (1 << 6), C2_BUF_INIT0 },
	};

	c2_buf_init(&test[ARRAY_SIZE(test) - 1].buf, i,
		    ARRAY_SIZE(i) * sizeof(i));
	c2_buf_init(&test[ARRAY_SIZE(test) - 2].buf, i,
		    ARRAY_SIZE(i) * sizeof(i));

	for (k = 0; k < ARRAY_SIZE(test); ++k) {
		rc = c2_buf_copy(&copy, &test[k].buf);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(c2_buf_eq(&copy, &test[k].buf));

		c2_free(copy.b_addr);
		copy.b_addr = NULL;
		copy.b_nob = 0;
	}

	for (k = 0; k < ARRAY_SIZE(test); ++k) {
		for (j = 0; j < ARRAY_SIZE(test); ++j) {
			equal = c2_buf_eq(&test[j].buf, &test[k].buf);
			C2_UT_ASSERT(equal == bit_is_set(test[j].equality_mask,
							 k));
		}
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
