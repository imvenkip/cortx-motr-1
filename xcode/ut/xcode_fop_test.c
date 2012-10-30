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

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/bitstring.h"
#include "lib/misc.h"
#include "lib/vec.h"
#include "lib/ut.h"
#include "colibri/init.h"
#include "fop/fop.h"
#include "xcode/ut/xcode_fops_ff.h"

#include "rpc/rpc_opcodes.h"
#include "net/net.h"

/** Random test values. */
enum {
	ARR_COUNT_1     = 10,
	ARR_COUNT_2     = 11,
	TEST_OFFSET     = 0xABCDEF,
	TEST_COUNT      = 0x123456,
	TEST_INDEX      = 0xDEAD,
	TEST_VAL        = 0x1111,
	TEST_CNT_1      = 0x1234,
	TEST_FLAG       = 0x1,
	TEST_BUF_SIZE   = 33,
	NO_OF_BUFFERS   = 85,
	BUFVEC_SEG_SIZE = 256
};

static char *fop_test_buf = "test fop encode/decode";

struct c2_fop_type_ops test_ops = {
};

static struct c2_fop_type c2_fop_test_fopt;

static void fop_verify( struct c2_fop *fop)
{
	void		   *fdata;
	struct c2_fop_test *ftest;
	int		    i;
	int                 j;

	fdata = c2_fop_data(fop);
	ftest = (struct c2_fop_test *)fdata;
	C2_UT_ASSERT(ftest->ft_cnt == TEST_COUNT);
	C2_UT_ASSERT(ftest->ft_offset == TEST_OFFSET);
	C2_UT_ASSERT(ftest->ft_arr.fta_cnt == ARR_COUNT_1);
	C2_UT_ASSERT(ftest->ft_arr.fta_data->da_cnt == ARR_COUNT_2);
	for (i = 0; i < ftest->ft_arr.fta_cnt; ++i) {
		int      index    = TEST_INDEX;
		int      test_val = TEST_VAL;
		uint32_t test_cnt = TEST_CNT_1;

		for (j = 0; j < ftest->ft_arr.fta_data->da_cnt; ++j) {
			int       cnt;
			uint64_t  temp;
			char     *c;

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
			c = (char *)
			     ftest->ft_arr.fta_data[i].da_pair[j].p_buf.tb_buf;
			temp = strcmp(c, fop_test_buf);
			C2_UT_ASSERT(temp == 0);
		}
	}
}

/** Clean up allocated fop structures. */
static void fop_free(struct c2_fop *fop)
{
	struct c2_fop_test *ccf1;
	unsigned int	    i;
	unsigned int	    j;

	ccf1 = c2_fop_data(fop);
	for (i = 0; i < ccf1->ft_arr.fta_cnt; ++i) {
		for (j = 0; j < ccf1->ft_arr.fta_data->da_cnt; ++j) {
			uint8_t *test_buf;

			test_buf =
			ccf1->ft_arr.fta_data[i].da_pair[j].p_buf.tb_buf;
			c2_free(test_buf);
		}
	}
        for (i = 0; i < ccf1->ft_arr.fta_cnt; ++i)
		c2_free(ccf1->ft_arr.fta_data[i].da_pair);

	c2_free(ccf1->ft_arr.fta_data);
	c2_free(c2_fop_data(fop));
	c2_free(fop);
}

/*
  Manually calculate the size of the fop based on the .ff file.
  For the current "test_fop" defined in xcode/ut/test.ff, we have -

  struct c2_test_buf {
        uint32_t tb_cnt(33);              4
        uint8_t *tb_buf;                + 33 (tb_cnt * uint8_t = 33 * 1)
  };                                    = 37

  struct c2_test_key {
        uint32_t tk_index;                4
        uint64_t tk_val;                + 8
        uint8_t tk_flag;                + 1
  };                                    = 13

  struct c2_pair {
        uint64_t p_offset;                8
        uint32_t p_cnt;                 + 4
        struct c2_test_key p_key;       + 13
        struct c2_test_buf p_buf;       + 37
  };                                    = 62

  struct c2_desc_arr {
        uint32_t da_cnt(11);              4
        struct c2_pair *da_pair;        + 682 (da_cnt * da_pair =  11 * 62)
  };                                    = 686

  struct c2_fop_test_arr {
        uint32_t fta_cnt(10);             4
        struct c2_desc_arr *fta_data;   + 6860 (fta_cnt * fta_data = 10 * 686)
  };                                    = 6864

  struct c2_fop_test {
        uint32_t ft_cnt;                  4
        uint64_t ft_offset;             + 8
        struct c2_fop_test_arr ft_arr;  + 6864
  };                                    = 6876

 */

/** Test function to check generic fop encode decode */
static void test_fop_encdec(void)
{
	int                      rc;
	struct c2_bufvec_cursor  cur;
	void                    *cur_addr;
	int                      i;
	int                      j;
	struct c2_fop           *f1;
	struct c2_fop           *fd1;
	struct c2_net_buffer    *nb;
	struct c2_fop_test      *ccf1;
	struct c2_xcode_ctx      xctx;
	struct c2_xcode_ctx      xctx1;
	size_t                   fop_size;
	size_t                   act_fop_size = 6876;
	size_t                   allocated;

	allocated = c2_allocated();

	rc = C2_FOP_TYPE_INIT(&c2_fop_test_fopt,
			      .name      = "xcode fop test",
			      .opcode    = C2_XCODE_UT_OPCODE,
			      .xt        = c2_fop_test_xc,
			      .rpc_flags = 0,
			      .fop_ops   = &test_ops);
	C2_UT_ASSERT(rc == 0);

	/* Allocate a fop and populate its fields with test values. */
	f1 = c2_fop_alloc(&c2_fop_test_fopt, NULL);
	C2_UT_ASSERT(f1 != NULL);

	ccf1 = c2_fop_data(f1);
	C2_ASSERT(ccf1 != NULL);
	ccf1->ft_arr.fta_cnt = ARR_COUNT_1;
	ccf1->ft_cnt    = TEST_COUNT;
	ccf1->ft_offset = TEST_OFFSET;
	C2_ALLOC_ARR(ccf1->ft_arr.fta_data, ccf1->ft_arr.fta_cnt);
	C2_UT_ASSERT(ccf1->ft_arr.fta_data != NULL);

        for (i = 0; i < ccf1->ft_arr.fta_cnt; ++i) {
		ccf1->ft_arr.fta_data[i].da_cnt=ARR_COUNT_2;
		C2_ALLOC_ARR(ccf1->ft_arr.fta_data[i].da_pair,
		     ccf1->ft_arr.fta_data[i].da_cnt);
		C2_UT_ASSERT(ccf1->ft_arr.fta_data[i].da_pair != NULL);
	}

	for (i = 0; i < ccf1->ft_arr.fta_cnt; ++i) {
		uint64_t ival  = TEST_VAL;
		int      index = TEST_INDEX;
		char     flag  = TEST_FLAG;
		uint32_t cnt   = TEST_CNT_1;

		for (j = 0; j < ccf1->ft_arr.fta_data->da_cnt; ++j) {
			uint8_t *test_buf;

			ccf1->ft_arr.fta_data[i].da_pair[j].p_offset = ival++;
			ccf1->ft_arr.fta_data[i].da_pair[j].p_cnt = cnt++;
			ccf1->ft_arr.fta_data[i].da_pair[j].p_key.tk_index =
				index++;
			ccf1->ft_arr.fta_data[i].da_pair[j].p_key.tk_val   =
				index++;
			ccf1->ft_arr.fta_data[i].da_pair[j].p_key.tk_flag  =
				flag;
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

	/* Check the size of the fop using the interfaces. */
	c2_xcode_ctx_init(&xctx, &(struct c2_xcode_obj) {
			  f1->f_type->ft_xt, ccf1 });
	fop_size = c2_xcode_length(&xctx);
	C2_UT_ASSERT(fop_size == act_fop_size);

	/* Allocate a netbuf and a bufvec, check alignments. */
	C2_ALLOC_PTR(nb);
        c2_bufvec_alloc(&nb->nb_buffer, NO_OF_BUFFERS, BUFVEC_SEG_SIZE);
        c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
        cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));

	c2_xcode_ctx_init(&xctx, &(struct c2_xcode_obj) {
			  f1->f_type->ft_xt, ccf1 });
	c2_bufvec_cursor_init(&xctx.xcx_buf, &nb->nb_buffer);

	/* Encode the fop into the bufvec. */
	rc = c2_xcode_encode(&xctx);
	C2_UT_ASSERT(rc == 0);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));
	/*
	   Allocate a fop for decode. The payload from the bufvec will be
	   decoded into this fop.
	   Since this is a decode fop we do not allocate fop->f_data.fd_data
	   since this allocation is done by xcode.
	   For more, see comments in c2_fop_item_type_default_decode()
	 */
	C2_ALLOC_PTR(fd1);
	C2_UT_ASSERT(fd1 != NULL);
	c2_fop_init(fd1, &c2_fop_test_fopt, NULL);
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));

	/* Decode the payload from bufvec into the fop. */
	c2_xcode_ctx_init(&xctx1, &(struct c2_xcode_obj) {
			  fd1->f_type->ft_xt, NULL });
	xctx1.xcx_alloc = c2_xcode_alloc;
	xctx1.xcx_buf   = cur;
	rc = c2_xcode_decode(&xctx1);
	C2_UT_ASSERT(rc == 0);
	fd1->f_data.fd_data = c2_xcode_ctx_top(&xctx1);

	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));

	/* Verify the fop data. */
	fop_verify(fd1);

	/* Clean up and free all the allocated memory. */
	c2_bufvec_free(&nb->nb_buffer);
	c2_free(nb);
	fop_free(f1);
	fop_free(fd1);
	c2_fop_type_fini(&c2_fop_test_fopt);
	C2_UT_ASSERT(allocated == c2_allocated());
}

static int xcode_bufvec_fop_init(void)
{
	c2_xc_xcode_fops_init();
	return 0;
}

static int xcode_bufvec_fop_fini(void)
{
	c2_xc_xcode_fops_fini();
	return 0;
}

const struct c2_test_suite xcode_bufvec_fop_ut = {
	.ts_name  = "xcode_bufvec_fop-ut",
	.ts_init  = xcode_bufvec_fop_init,
	.ts_fini  = xcode_bufvec_fop_fini,
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
