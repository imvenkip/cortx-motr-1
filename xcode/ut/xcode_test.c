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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 08/5/2011
 */
#include <limits.h>
#include "lib/vec.h"
#include "lib/arith.h"
#include "lib/memory.h"
#include "xcode/bufvec_xcode.h"
#include "lib/ut.h"

enum {
	NO_OF_ELEMENTS = 50,
};

static struct c2_bufvec		vec;
static struct c2_bufvec_cursor	cur;

static void test_uint32_encode()
{
	int	 rc;
	void    *cur_addr;
	uint32_t enc_val;

	enc_val = UINT_MAX;
	rc = c2_bufvec_uint32(&cur, &enc_val, C2_BUFVEC_ENCODE);
	C2_UT_ASSERT(rc == 0);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	enc_val = INT_MIN;
	rc = c2_bufvec_uint32(&cur, &enc_val, C2_BUFVEC_ENCODE);
	C2_UT_ASSERT(rc == 0);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));
}

static void test_uint32_decode()
{
	int		 rc;
	uint32_t	 dec_val;
	void		*cur_addr;

	rc = c2_bufvec_uint32(&cur, &dec_val, C2_BUFVEC_DECODE);
	C2_UT_ASSERT(rc == 0 && dec_val == UINT_MAX);

	rc = c2_bufvec_uint32(&cur, &dec_val, C2_BUFVEC_DECODE);
	C2_UT_ASSERT(rc == 0 && (int)dec_val == INT_MIN);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));
}

static void test_uint64_encode()
{
	int	 rc;
	void    *cur_addr;
	uint64_t enc_val;

	enc_val = ULLONG_MAX ;
	cur_addr = c2_bufvec_cursor_addr(&cur);
	rc = c2_bufvec_uint64(&cur, &enc_val, C2_BUFVEC_ENCODE);
	C2_UT_ASSERT(rc == 0);
	enc_val = LLONG_MIN;
	cur_addr = c2_bufvec_cursor_addr(&cur);
	rc = c2_bufvec_uint64(&cur, &enc_val, C2_BUFVEC_ENCODE);
	C2_UT_ASSERT(rc == 0);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));
}

static void test_uint16_encode()
{
	int	 rc;
	void    *cur_addr;
	uint16_t enc_val;

	enc_val = USHRT_MAX;
	rc = c2_bufvec_uint16(&cur, &enc_val, C2_BUFVEC_ENCODE);
	C2_UT_ASSERT(rc == 0);
	enc_val = SHRT_MIN;
	rc = c2_bufvec_uint16(&cur, &enc_val, C2_BUFVEC_ENCODE);
	C2_UT_ASSERT(rc == 0);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));
}

static void test_uint64_decode()
{
	int		rc;
	uint64_t	dec_val;
	void	       *cur_addr;

	rc = c2_bufvec_uint64(&cur, &dec_val, C2_BUFVEC_DECODE);
	C2_UT_ASSERT(rc == 0 && dec_val == ULLONG_MAX);

	rc = c2_bufvec_uint64(&cur, &dec_val, C2_BUFVEC_DECODE);
	C2_UT_ASSERT(rc == 0 && (long)dec_val == LLONG_MIN);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));
}

static void test_uint16_decode()
{
	int		 rc;
	uint16_t	 dec_val;
	void		*cur_addr;

	rc = c2_bufvec_uint16(&cur, &dec_val, C2_BUFVEC_DECODE);
	C2_UT_ASSERT(rc == 0 && dec_val == USHRT_MAX);

	rc = c2_bufvec_uint16(&cur, &dec_val, C2_BUFVEC_DECODE);
	C2_UT_ASSERT(rc == 0 && (short)dec_val == SHRT_MIN);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));
}

static void xcode_bufvec_test(void)
{
	void	*cur_addr;

	c2_bufvec_alloc(&vec, 40, 40);
	c2_bufvec_cursor_init(&cur, &vec);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));
	/* Encode tests */
	test_uint32_encode();
	test_uint64_encode();
	test_uint16_encode();
	c2_bufvec_cursor_init(&cur, &vec);
	/* Decode tests */
	test_uint32_decode();
	test_uint64_decode();
	test_uint16_decode();
	c2_bufvec_free(&vec);
}

const struct c2_test_suite xcode_bufvec_ut = {
        .ts_name = "xcode_bufvec-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "xcode-bufvec", xcode_bufvec_test },
                { NULL, NULL }
        }
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
