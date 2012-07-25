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
static uint32_t                 test_arr[NO_OF_ELEMENTS];
static char                     *byte_arr = "bufvec encode/decode tests";

static void test_arr_encode()
{
	int		rc, i;
	size_t		el_size;
	void		*cur_addr;

	for ( i = 0; i < NO_OF_ELEMENTS; ++i)
		test_arr[i] = i;

	el_size = sizeof test_arr[0];
	rc = c2_bufvec_array(&cur, test_arr, NO_OF_ELEMENTS, ~0, el_size,
			    (c2_bufvec_xcode_t)c2_bufvec_uint32, C2_BUFVEC_ENCODE);
	C2_UT_ASSERT(rc == 0);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));
}

static void test_arr_decode()
{
	int		rc;
	size_t		el_size;
	uint32_t	dec_arr[NO_OF_ELEMENTS];
	void		*cur_addr;

	el_size = sizeof dec_arr[0];
	rc = c2_bufvec_array(&cur, dec_arr, NO_OF_ELEMENTS, ~0, el_size,
			    (c2_bufvec_xcode_t)c2_bufvec_uint32,
			    C2_BUFVEC_DECODE);
	C2_UT_ASSERT(rc == 0);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));

	rc = memcmp(test_arr, dec_arr, sizeof test_arr);
	C2_UT_ASSERT(rc == 0);
}

static void test_byte_arr_encode()
{
	int	rc;
	void    *cur_addr;

	rc = c2_bufvec_bytes(&cur, &byte_arr, strlen(byte_arr), ~0,
			     C2_BUFVEC_ENCODE);
	C2_UT_ASSERT(rc == 0);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));
}

static void test_byte_arr_decode()
{
	int		 rc;
	void		*cur_addr;
	char		*byte_arr_decode;
	size_t		 arr_size;

	arr_size = strlen(byte_arr);
	C2_ALLOC_ARR(byte_arr_decode, arr_size);
	rc = c2_bufvec_bytes(&cur, &byte_arr_decode, arr_size, ~0,
			     C2_BUFVEC_DECODE);
	C2_UT_ASSERT(rc == 0);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));
	c2_free(byte_arr_decode);
}
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

static void test_byte_encode()
{
	int	 rc;
	void    *cur_addr;
	uint8_t  enc_val;

	enc_val = UCHAR_MAX;
	rc = c2_bufvec_byte(&cur, &enc_val, C2_BUFVEC_ENCODE);
	C2_UT_ASSERT(rc == 0);
	enc_val = SCHAR_MIN;;
	rc = c2_bufvec_byte(&cur, &enc_val, C2_BUFVEC_ENCODE);
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

static void test_byte_decode()
{
	int		 rc;
	uint8_t		 dec_val;
	void		*cur_addr;

	rc = c2_bufvec_byte(&cur, &dec_val, C2_BUFVEC_DECODE);
	C2_UT_ASSERT(rc == 0 && dec_val == UCHAR_MAX);

	rc = c2_bufvec_byte(&cur, &dec_val, C2_BUFVEC_DECODE);
	C2_UT_ASSERT(rc == 0 && (char)dec_val == SCHAR_MIN);
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
	test_byte_encode();
	test_arr_encode();
	test_byte_arr_encode();
	c2_bufvec_cursor_init(&cur, &vec);
	/* Decode tests */
	test_uint32_decode();
	test_uint64_decode();
	test_uint16_decode();
	test_byte_decode();
	test_arr_decode();
	test_byte_arr_decode();
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


