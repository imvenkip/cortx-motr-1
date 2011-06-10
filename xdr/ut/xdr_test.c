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
 * Original author: Subhash Arya <Subhash Arya@xyratex.com>
 * Original creation date: 06/02/2011
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include "xdr/xdr_rec.h"
#include "lib/errno.h"
#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/memory.h"
#include "lib/assert.h"

enum {
	MAX_BUF_SIZE = 140,
	SEND_SIZE    = 120,
	RECV_SIZE    = 120,
	UB_ITER      = 5000000
};

/* Use C2_ASSERT for UB since C2_UT_ASSERT doesn't work for UB */
#define XDR_TEST_ASSERT(cond)				\
	if(ub_flag)					\
		C2_ASSERT(cond);			\
	else						\
		C2_UT_ASSERT(cond);

/* The various tests */
#define XDR_ENCODE_TESTS                                \
        TEST_XDR_ENCODE(int, 0)                         \
        TEST_XDR_ENCODE(int, INT_MAX)                   \
        TEST_XDR_ENCODE(int, INT_MIN)                   \
        TEST_XDR_ENCODE(u_int, 0)                       \
        TEST_XDR_ENCODE(u_int, UINT_MAX)                \
        TEST_XDR_ENCODE(long, 0)                        \
        TEST_XDR_ENCODE(long, 2147483647L)              \
        TEST_XDR_ENCODE(long, -2147483648L)             \
        TEST_XDR_ENCODE(u_long, 0)                      \
        TEST_XDR_ENCODE(u_long, 0xffffffffUL)           \
        TEST_XDR_ENCODE(short, SHRT_MAX)                \
        TEST_XDR_ENCODE(short, SHRT_MIN)                \
        TEST_XDR_ENCODE(u_short, 0)                     \
        TEST_XDR_ENCODE(u_short, USHRT_MAX)             \
        TEST_XDR_ENCODE(char, CHAR_MAX)                 \
        TEST_XDR_ENCODE(char, CHAR_MIN)                 \
        TEST_XDR_ENCODE(u_char, 0)                      \
        TEST_XDR_ENCODE(u_char, UCHAR_MAX)              \
        TEST_XDR_ENCODE(bool, 0)                        \
        TEST_XDR_ENCODE(bool, 1)                        \
        TEST_XDR_ENCODE(enum, 0)                        \
        TEST_XDR_ENCODE(enum, INT_MAX)                  \
        TEST_XDR_ENCODE(enum, INT_MIN)                  \

/* The xdr encode  test macro */
#define TEST_XDR_ENCODE( type, value )                  \
        test_##type = value;                            \
        result = xdr_##type(xdrs, &test_##type);        \
	XDR_TEST_ASSERT(result == 1);

#define XDR_DECODE_TESTS                                \
        TEST_XDR_DECODE(int, 0)                         \
        TEST_XDR_DECODE(int, INT_MAX)                   \
        TEST_XDR_DECODE(int, INT_MIN)                   \
        TEST_XDR_DECODE(u_int, 0)                       \
        TEST_XDR_DECODE(u_int, UINT_MAX)                \
        TEST_XDR_DECODE(long, 0)                        \
        TEST_XDR_DECODE(long, 2147483647L)              \
        TEST_XDR_DECODE(long, -2147483648L)             \
        TEST_XDR_DECODE(u_long, 0)                      \
        TEST_XDR_DECODE(u_long, 0xffffffffUL)           \
        TEST_XDR_DECODE(short, SHRT_MAX)                \
        TEST_XDR_DECODE(short, SHRT_MIN)                \
        TEST_XDR_DECODE(u_short, 0)                     \
        TEST_XDR_DECODE(u_short, USHRT_MAX)             \
        TEST_XDR_DECODE(char, CHAR_MAX)                 \
        TEST_XDR_DECODE(char, CHAR_MIN)                 \
        TEST_XDR_DECODE(u_char, 0)                      \
        TEST_XDR_DECODE(u_char, UCHAR_MAX)              \
        TEST_XDR_DECODE(bool, 0)                        \
        TEST_XDR_DECODE(bool, 1)                        \
        TEST_XDR_DECODE(enum, 0)                        \
        TEST_XDR_DECODE(enum, INT_MAX)                  \
        TEST_XDR_DECODE(enum, INT_MIN)

#define TEST_XDR_DECODE( type, value )                  \
        test_##type = 0x15;                             \
        result = xdr_##type ( xdrs, &test_##type );     \
	XDR_TEST_ASSERT(result == 1);			\
	XDR_TEST_ASSERT(test_##type == value);

struct xdr_test_handle {
        int                     xt_sock;
        /* The XDR stream handle for this connection to be used for
	   marshalling/unmarshalling */
        XDR                     xt_xdrs;
};


static XDR *xdrs;
static int fd;
static bool ub_flag;
static struct xdr_test_handle *xt;

/**
  Read and Write functions for the XDR stream. Instead of writing the data to
  a TCP stream, we are writing it to a file.
*/

static int write_file(char *cptr, char *buf, int len)
{
        int count, fd;
        struct xdr_test_handle *xt_ptr;

        xt_ptr = (struct xdr_test_handle *)cptr;
        fd = xt_ptr->xt_sock;

        if (len == 0)
                return 0;
        count = write(fd, buf, len);
        return count;
}

static int read_file(char *cptr, char *buf, int len)
{
        int count, fd;
        struct xdr_test_handle *xt_ptr;

        xt_ptr = (struct xdr_test_handle *)cptr;
        fd = xt_ptr->xt_sock;

        if (len == 0)
                return 0;
        count = read(fd, buf, len);
        return count;
}

static void ub_xdr_init()
{
	ub_flag = true;
	C2_ALLOC_PTR(xt);
	fd = open("xdr_stream.txt", O_CREAT|O_RDWR, S_IRUSR | S_IWUSR);
	XDR_TEST_ASSERT(fd >= 0);
	xt->xt_sock = fd;
	xdrs = &(xt->xt_xdrs);
	xdrrec_create(xdrs, SEND_SIZE, RECV_SIZE,
	(caddr_t)xt, read_file, write_file);
	xdrs->x_ops = (struct xdr_ops *)&c2_xdrrec_ops;
}

static void ub_xdr_fin()
{
	ub_flag = false;
	close(fd);
	c2_free(xt);
	xdr_destroy(xdrs);
}

/* Unit benchmark tests for each data type */

static void ub_encode_init()
{
	ub_xdr_init();
	xdrs->x_op = XDR_ENCODE;
}

static void ub_encode_fin()
{
	xdrrec_endofrecord(xdrs, TRUE);
	ub_xdr_fin();
}

static void ub_decode_init()
{
	ub_xdr_init();
	xdrs->x_op = XDR_DECODE;
	xdrrec_skiprecord(xdrs);
}

static void ub_decode_fin()
{
	ub_xdr_fin();
}

static void ub_encode_uint32(int i)
{
	uint32_t test_uint32_t;
	int	 result;
	TEST_XDR_ENCODE( uint32_t, UINT_MAX );
}

static void ub_encode_uint64(int i)
{
	uint64_t test_uint64_t;
	int	 result;
	TEST_XDR_ENCODE(uint64_t, ULLONG_MAX);
}

static void ub_encode_uint16(int i)
{
	uint16_t test_uint16_t;
	int 	 result;
	TEST_XDR_ENCODE(uint16_t, USHRT_MAX);
}

static void ub_encode_uchar(int i)
{
	u_char test_u_char;
	int    result;
	TEST_XDR_ENCODE(u_char, UCHAR_MAX);
}

static void ub_encode_bool(int i)
{
	bool_t test_bool;
	int    result;
	TEST_XDR_ENCODE(bool, true);
}

static void ub_encode_enum(int i)
{
	enum_t test_enum;
	int    result;
	TEST_XDR_ENCODE(enum, INT_MAX);
}

static void ub_decode_uint32(int i)
{
	uint32_t test_uint32_t;
	int      result;
	TEST_XDR_DECODE(uint32_t, UINT_MAX);
}

static void ub_decode_uint64(int i)
{
	uint64_t test_uint64_t;
	int      result;
	TEST_XDR_DECODE(uint64_t, ULLONG_MAX);
}

static void ub_decode_uint16(int i)
{
	uint16_t test_uint16_t;
	int      result;
	TEST_XDR_DECODE(uint16_t, USHRT_MAX);
}

static void ub_decode_uchar(int i)
{
	u_char test_u_char;
	int    result;
	TEST_XDR_DECODE(u_char, UCHAR_MAX);
}

static void ub_decode_bool(int i)
{
	bool_t test_bool;
	int    result;
	TEST_XDR_DECODE(bool, true);
}

static void ub_decode_enum(int i)
{
	enum_t test_enum;
	int    result;
	TEST_XDR_DECODE(enum, INT_MAX);
}

static void fill_buf(char *buf)
{
        int i;
        char c = 'a'; /* fill the buf with  random charachters */
        for(  i = 0; i < MAX_BUF_SIZE; i++) {
                *buf = c++;
                buf++;
        }
}

static void xdr_client( void )
{
        int                     fd, result;
        u_int                   len;
        char                    *buf;
        XDR                     *xdrs;
        struct xdr_test_handle  *xt;

        /* XDR test data */
        int                     test_int;
        u_int                   test_u_int;
        long                    test_long;
        u_long                  test_u_long;
        short                   test_short;
        u_short                 test_u_short;
        char                    test_char;
        u_char                  test_u_char;
        bool_t                  test_bool;
        enum_t                  test_enum;

        C2_ALLOC_PTR(xt);
        C2_ALLOC_ARR( buf, MAX_BUF_SIZE );

        fd = open("xdr_stream.txt", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        C2_UT_ASSERT(fd >= 0 );
        xt->xt_sock = fd;
        xdrs = &(xt->xt_xdrs);
        xdrrec_create(xdrs, SEND_SIZE, RECV_SIZE,
                     (caddr_t)xt, read_file, write_file);
        xdrs->x_ops = (struct xdr_ops *)&c2_xdrrec_ops;
        xdrs->x_op = XDR_ENCODE;

        /* Everything is ready,  run our tests, start XDR encoding */
        XDR_ENCODE_TESTS;

        /* Test XDR record marking */
        fill_buf(buf);
        len = MAX_BUF_SIZE;
        xdr_bytes(xdrs, &buf, &len, MAX_BUF_SIZE);
        C2_UT_ASSERT(result == 1);
        xdrrec_endofrecord(xdrs, TRUE);

        /*  Cleanup */
        close( fd );
        c2_free(xt);
        c2_free(buf);
        xdr_destroy(xdrs);
}

static void xdr_server( void )
{
        int                     fd, result;
        u_int                   len;
        char                    *in_buf, *buf;
        XDR                     *xdrs;
        struct xdr_test_handle  *xt;

        /* XDR Test data */
        int                     test_int;
        u_int                   test_u_int;
        long                    test_long;
        u_long                  test_u_long;
        short                   test_short;
        u_short                 test_u_short;
        char                    test_char;
        u_char                  test_u_char;
        bool_t                  test_bool;
        enum_t                  test_enum;

        C2_ALLOC_PTR(xt);
        C2_ALLOC_ARR(buf, MAX_BUF_SIZE );
        C2_ALLOC_ARR(in_buf, MAX_BUF_SIZE );

        fd = open("xdr_stream.txt", O_RDWR);
        C2_UT_ASSERT(fd >= 0);
        xt->xt_sock = fd;
        xdrs = &(xt->xt_xdrs);
        xdrrec_create(xdrs, SEND_SIZE, RECV_SIZE,(caddr_t) xt,
                     read_file, write_file);
        xdrs->x_op = XDR_DECODE;
        xdrs->x_ops = (struct xdr_ops *)&c2_xdrrec_ops;
        xdrrec_skiprecord(xdrs);

        /* Start unmarshalling the XDR test data */
        XDR_DECODE_TESTS;

        /* Unmarshal XDR record marking test data */
        fill_buf(buf);
        len = MAX_BUF_SIZE;
        result = xdr_bytes(xdrs, &in_buf, &len, MAX_BUF_SIZE);
        C2_UT_ASSERT(result == 1);
        result = memcmp(buf, in_buf, MAX_BUF_SIZE);
        C2_UT_ASSERT(result == 0);

        /* Cleanup */
        close(fd);
        c2_free(buf);
        c2_free(in_buf);
        c2_free(xt);
        xdr_destroy(xdrs);
}

const struct c2_test_suite xdr_ut = {
        .ts_name = "xdr-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "xdr-client", xdr_client },
                { "xdr-server", xdr_server },
                { NULL, NULL }
        }
};

const struct c2_ub_set c2_xdr_ub = {
	.us_name = "xdr-ub",
	.us_init = NULL,
	.us_fini = NULL,
	.us_run  = {
		{ .ut_name  = "encode int16",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_encode_uint16,
		  .ut_init  = ub_encode_init },

		{ .ut_name  = "encode int32",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_encode_uint32 },

		{ .ut_name  = "encode int64",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_encode_uint64 },

		{ .ut_name  = "encode char",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_encode_uchar },

		{ .ut_name  = "encode bool",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_encode_bool },

		{ .ut_name  = "encode enum",
		  .ut_iter  = UB_ITER,
		  .ut_fini  = ub_encode_fin,
		  .ut_round = ub_encode_enum },

		{ .ut_name  = "decode int16",
		  .ut_iter  = UB_ITER,
		  .ut_init  = ub_decode_init,
		  .ut_round = ub_decode_uint16 },

		{ .ut_name  = "decode int32",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_decode_uint32 },

		{ .ut_name  = "decode int64",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_decode_uint64 },

		{ .ut_name  = "decode char",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_decode_uchar },

		{ .ut_name  = "decode bool",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_decode_bool },

		{ .ut_name  = "decode enum",
		  .ut_iter  = UB_ITER,
		  .ut_round = ub_decode_enum,
		  .ut_fini  = ub_decode_fin },

		{ .ut_name = NULL }
	}
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

