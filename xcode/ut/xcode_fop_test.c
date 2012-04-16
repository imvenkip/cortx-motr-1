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
 * Original creation date: 06/25/2011
 */

#include <stdio.h>
#include "lib/errno.h"
#include "colibri/init.h"
#include "lib/memory.h"
#include "lib/bitstring.h"
#include "lib/misc.h"
#include "fop/fop.h"
#include "lib/ut.h"
#include "fop/fop_format_def.h"
#include "fop/fop_format.h"
#include "fop/fop_base.h"
#include "xcode/bufvec_xcode.h"
#include "xcode/ut/test_u.h"
#include "xcode/ut/test.ff"
#include "lib/vec.h"
#include "rpc/rpc_opcodes.h"

/** Random test values. */
enum {
	ARR_COUNT_1 = 10,
	ARR_COUNT_2 = 11,
	TEST_OFFSET = 0xABCDEF,
	TEST_COUNT  = 0x123456,
	TEST_INDEX  = 0xDEAD,
	TEST_VAL    = 0x1111,
	TEST_CNT_1  = 0x1234,
	TEST_FLAG   = 0x1,
	TEST_BUF_SIZE = 33,
	NO_OF_BUFFERS = 85,
	BUFVEC_SEG_SIZE = 256
};

static char *fop_test_buf = "test fop encode/decode";

extern struct c2_fop_type_format c2_fop_test_tfmt;

struct c2_fop_type_ops test_ops = {
};

C2_FOP_TYPE_DECLARE(c2_fop_test, "test", &test_ops, C2_XCODE_UT_OPCODE, 0);

static void fop_verify( struct c2_fop *fop)
{
	void		      *fdata;
	struct c2_fop_test    *ftest;
	int		       i, j;

	fdata = c2_fop_data(fop);
	ftest = (struct c2_fop_test *)fdata;
	C2_UT_ASSERT(ftest->ft_cnt == TEST_COUNT);
	C2_UT_ASSERT(ftest->ft_offset == TEST_OFFSET);
	C2_UT_ASSERT(ftest->ft_arr.fta_cnt == ARR_COUNT_1);
	C2_UT_ASSERT(ftest->ft_arr.fta_data->da_cnt == ARR_COUNT_2);
	for(i = 0; i < ftest->ft_arr.fta_cnt; ++i) {
                int index = TEST_INDEX;
                uint32_t test_cnt = TEST_CNT_1;
		int test_val = TEST_VAL;
		for (j = 0; j < ftest->ft_arr.fta_data->da_cnt; ++j) {
                        int  cnt;
			uint64_t temp;
			char *c;

			temp = ftest->ft_arr.fta_data[i].da_pair[j].p_offset;
			C2_UT_ASSERT(temp == test_val);
			test_val++;
			temp = ftest->ft_arr.fta_data[i].da_pair[j].p_cnt;
			C2_UT_ASSERT(temp == test_cnt);
			test_cnt++;
			temp =
			ftest->ft_arr.fta_data[i].da_pair[j].p_key.tk_index;
			C2_UT_ASSERT(temp == index);
			index++;
			temp =
			ftest->ft_arr.fta_data[i].da_pair[j].p_key.tk_val;
			C2_UT_ASSERT(temp == index);
			index++;
			temp =
			ftest->ft_arr.fta_data[i].da_pair[j].p_key.tk_flag;
			C2_UT_ASSERT(temp == TEST_FLAG);
			cnt = ftest->ft_arr.fta_data[i].da_pair[j].p_buf.tb_cnt;
			C2_UT_ASSERT(cnt == TEST_BUF_SIZE);
			c = ftest->ft_arr.fta_data[i].da_pair[j].p_buf.tb_buf;
			temp = strcmp(c, fop_test_buf);
			C2_UT_ASSERT(temp == 0);
		}
	}
}

/** Clean up allocated fop structures. */
static void fop_free(struct c2_fop *fop)
{
	struct c2_fop_test	*ccf1;
	int			 i;
	int			 j;

	ccf1 = c2_fop_data(fop);
	for(i = 0; i < ccf1->ft_arr.fta_cnt; ++i) {
		for (j = 0; j < ccf1->ft_arr.fta_data->da_cnt; ++j) {
			char *test_buf;
			test_buf =
			ccf1->ft_arr.fta_data[i].da_pair[j].p_buf.tb_buf;
			c2_free(test_buf);
		}
	}
        for(i = 0; i < ccf1->ft_arr.fta_cnt; ++i)
		c2_free(ccf1->ft_arr.fta_data[i].da_pair);

	c2_free(ccf1->ft_arr.fta_data);
	c2_fop_free(fop);
}

/** Test function to check generic fop encode decode */
static void test_fop_encdec(void)
{
	int			 rc;
	struct c2_bufvec_cursor	 cur;
	void			*cur_addr;
	int			 i;
	int			 j;
	struct c2_fop		*f1;
	struct c2_fop		*fd1;
	struct c2_net_buffer	*nb;
	struct c2_fop_test	*ccf1;
	size_t			 fop_size;
	size_t			 act_fop_size;
	size_t			 allocated;

	allocated = c2_allocated();
	rc = c2_fop_type_format_parse(&c2_test_buf_tfmt);
	C2_UT_ASSERT(rc == 0);
	rc = c2_fop_type_format_parse(&c2_test_key_tfmt);
	C2_UT_ASSERT(rc == 0);
	rc = c2_fop_type_format_parse(&c2_pair_tfmt);
	C2_UT_ASSERT(rc == 0);
	rc = c2_fop_type_format_parse(&c2_desc_arr_tfmt);
	C2_UT_ASSERT(rc == 0);
	rc = c2_fop_type_format_parse(&c2_fop_test_arr_tfmt);
	C2_UT_ASSERT(rc == 0);
	rc = c2_fop_type_build(&c2_fop_test_fopt);
	C2_UT_ASSERT(rc == 0);

	/* Allocate a fop and populate its fields with test values. */
	f1 = c2_fop_alloc(&c2_fop_test_fopt, NULL);
	C2_UT_ASSERT(f1 != NULL);

	ccf1 = c2_fop_data(f1);
	C2_ASSERT(ccf1 != NULL);
	ccf1->ft_arr.fta_cnt = ARR_COUNT_1;
	ccf1->ft_cnt = TEST_COUNT;
	ccf1->ft_offset = TEST_OFFSET;
	C2_ALLOC_ARR(ccf1->ft_arr.fta_data, ccf1->ft_arr.fta_cnt);
	C2_UT_ASSERT(ccf1->ft_arr.fta_data != NULL);

        for(i = 0; i < ccf1->ft_arr.fta_cnt; ++i) {
		ccf1->ft_arr.fta_data[i].da_cnt=ARR_COUNT_2;
		C2_ALLOC_ARR(ccf1->ft_arr.fta_data[i].da_pair,
		     ccf1->ft_arr.fta_data[i].da_cnt);
		C2_UT_ASSERT(ccf1->ft_arr.fta_data[i].da_pair != NULL);
	}

	for(i = 0; i < ccf1->ft_arr.fta_cnt; ++i) {
		uint64_t ival = TEST_VAL;
		int index = TEST_INDEX;
		char flag = TEST_FLAG;
		uint32_t cnt = TEST_CNT_1;
		for (j = 0; j < ccf1->ft_arr.fta_data->da_cnt; ++j) {
			char *test_buf;
			ccf1->ft_arr.fta_data[i].da_pair[j].p_offset = ival++;
			ccf1->ft_arr.fta_data[i].da_pair[j].p_cnt = cnt++;
			ccf1->ft_arr.fta_data[i].da_pair[j].p_key.tk_index
			= index++;
			ccf1->ft_arr.fta_data[i].da_pair[j].p_key.tk_val
			= index++;
			ccf1->ft_arr.fta_data[i].da_pair[j].p_key.tk_flag
			= flag;
			C2_ALLOC_ARR(test_buf, TEST_BUF_SIZE);
			C2_UT_ASSERT(test_buf != NULL);
			ccf1->ft_arr.fta_data[i].da_pair[j].p_buf.tb_buf =
			test_buf;
			ccf1->ft_arr.fta_data[i].da_pair[j].p_buf.tb_cnt =
			TEST_BUF_SIZE;
			memcpy(ccf1->ft_arr.fta_data[i].da_pair[j].p_buf.tb_buf,
			fop_test_buf, strlen(fop_test_buf));
		}
	}

	/* Manually calculate the size of the fop based on the .ff file.
	  For the current "test_fop" defined in fop.ff, we can calculate
	  the size of the fop using the formula given below. */
	act_fop_size = 24 + ARR_COUNT_1 * (8 + ARR_COUNT_2 * 88);

	/*Check the size of the fop using the interfaces. */
	fop_size = c2_xcode_fop_size_get(f1);
	C2_UT_ASSERT(fop_size == act_fop_size);

	/* Allocate a netbuf and a bufvec, check alignments. */
	C2_ALLOC_PTR(nb);
        c2_bufvec_alloc(&nb->nb_buffer, NO_OF_BUFFERS, BUFVEC_SEG_SIZE);
        c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
        cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));

	/* Encode the fop into the bufvec. */
	rc = c2_xcode_bufvec_fop(&cur, f1, C2_BUFVEC_ENCODE);
	C2_UT_ASSERT(rc == 0);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));

	/* Allocate a fop for decode. The payload from the bufvec will be
	   decoded into this fop. */
	fd1 = c2_fop_alloc(&c2_fop_test_fopt, NULL);
	C2_UT_ASSERT(fd1 != NULL);
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));

	/* Decode the payload from bufvec into the fop. */
	rc = c2_xcode_bufvec_fop(&cur, fd1, C2_BUFVEC_DECODE);
	C2_UT_ASSERT(rc == 0);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));

	/* Verify the fop data. */
	fop_verify(fd1);

	/* Clean up and free all the allocated memory. */
	c2_bufvec_free(&nb->nb_buffer);
	c2_free(nb);
	fop_free(f1);
	fop_free(fd1);
	c2_fop_type_format_fini(&c2_test_buf_tfmt);
	c2_fop_type_format_fini(&c2_test_key_tfmt);
	c2_fop_type_format_fini(&c2_pair_tfmt);
	c2_fop_type_format_fini(&c2_desc_arr_tfmt);
	c2_fop_type_format_fini(&c2_fop_test_arr_tfmt);
	c2_fop_type_fini(&c2_fop_test_fopt);
	C2_UT_ASSERT(allocated == c2_allocated());
}

const struct c2_test_suite xcode_bufvec_fop_ut = {
	.ts_name = "xcode_bufvec_fop-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "xcode_bufvec_fop", test_fop_encdec },
		{ NULL, NULL }
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
