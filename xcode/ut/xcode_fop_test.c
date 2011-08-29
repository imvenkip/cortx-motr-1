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

/** Random test values */
enum {
	ARR_COUNT_1 = 0x10,
	ARR_COUNT_2 = 0x9,
	TEST_OFFSET = 0xABCDEF,
	TEST_COUNT  = 0x123456,
	TEST_INDEX  = 0xDEAD,
	TEST_VAL    = 0x1111,
	TEST_FLAG   = 0x1,
	NO_OF_BUFFERS = 20,
	BUFVEC_SEG_SIZE = 256
};

extern struct c2_fop_type_format c2_fop_test_tfmt;
int test_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	printf("Called test_handler\n");
	return 0;
}

struct c2_fop_type_ops test_ops = {
        .fto_execute = test_handler,
};

C2_FOP_TYPE_DECLARE(c2_fop_test, "test", 60, &test_ops);

void print_fop( struct c2_fop *fop)
{
	void		      *fdata;
	struct c2_fop_test    *ftest;
	int		       i, j;

	fdata = c2_fop_data(fop);
	ftest = (struct c2_fop_test *)fdata;
	printf("ft_cnt = %x\n",  ftest->ft_cnt);
	printf("fta_offset = %lx\n", ftest->ft_offset);
	printf("fta_cnt = %x\n", ftest->ft_arr.fta_cnt);
	printf("da_cnt = %x\n", ftest->ft_arr.fta_data->da_cnt);
	for(i = 0; i < ftest->ft_arr.fta_cnt; ++i) {
		printf("i = %d\n", i);
		for (j = 0; j < ftest->ft_arr.fta_data->da_cnt; ++j) {
                        printf("p_offset = %lx ",
			ftest->ft_arr.fta_data[i].da_pair[j].p_offset);
                        printf("p_cnt = %x\n",
			ftest->ft_arr.fta_data[i].da_pair[j].p_cnt);
			printf("tk_index = %x ",
			ftest->ft_arr.fta_data[i].da_pair[j].p_key.tk_index);
			printf("tk_val = %lx ",
			ftest->ft_arr.fta_data[i].da_pair[j].p_key.tk_val);
			printf("tk_flag = %x ",
			ftest->ft_arr.fta_data[i].da_pair[j].p_key.tk_flag);
		}
        printf("\n");
	fflush(stdout);
	}
}

static void test_fop_encode(void)
{
	int				 rc;
	struct c2_bufvec_cursor		 cur;
	void				*cur_addr;
	int				 i;
	int				 j;
	struct c2_fop			*f1, *fd1;
	struct c2_net_buffer      	*nb;
	struct c2_fop_test    		*ccf1;

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
		int ival = TEST_VAL;
		int index = TEST_INDEX;
		char flag = TEST_FLAG;
		for (j = 0; j < ccf1->ft_arr.fta_data->da_cnt; ++j) {
			ccf1->ft_arr.fta_data[i].da_pair[j].p_offset = ival++;
			ccf1->ft_arr.fta_data[i].da_pair[j].p_cnt = ival++;
			ccf1->ft_arr.fta_data[i].da_pair[j].p_key.tk_index
			= index++;
			ccf1->ft_arr.fta_data[i].da_pair[j].p_key.tk_val
			= index++;
			ccf1->ft_arr.fta_data[i].da_pair[j].p_key.tk_flag
			= flag;
		}
	}

	C2_ALLOC_PTR(nb);
        c2_bufvec_alloc(&nb->nb_buffer, NO_OF_BUFFERS, BUFVEC_SEG_SIZE);
        c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
        cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));

	rc = c2_xcode_bufvec_fop(&cur, f1, C2_BUFVEC_ENCODE);
	C2_UT_ASSERT(rc == 0);
	//cur_addr = c2_bufvec_cursor_addr(&cur);
	//C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));

	fd1 = c2_fop_alloc(&c2_fop_test_fopt, NULL);
	C2_UT_ASSERT(fd1 != NULL);

	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));

	rc = c2_xcode_bufvec_fop(&cur, fd1, C2_BUFVEC_DECODE);
	C2_UT_ASSERT(rc == 0);
	//cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));
	print_fop(fd1);
	c2_fop_type_fini(&c2_fop_test_fopt);
}

const struct c2_test_suite xcode_bufvec_fop_ut = {
	.ts_name = "bufvec_xcode_fop-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "fop_bufvec_enc", test_fop_encode },
	//	{ "fop_bufvec_dec", test_fop_decode },
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
