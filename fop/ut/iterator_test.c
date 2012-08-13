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
 * Original creation date: 01/05/2011
 */


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/errno.h"
#include "lib/ut.h"
#include "lib/cdefs.h"
#include "lib/memory.h"

#include "fop/fop.h"
#include "iterator_test_xc.h"
#include "rpc/rpc_opcodes.h"
#include "xcode/xcode.h"


/* FOP object iterator test tests iterations of the following types:
 *   - FFA_RECORD,
 *   - FFA_SEQUENCE.
 */

static struct c2_fop_type c2_fop_iterator_test_fopt;

static void fop_fini(void)
{
	c2_fop_type_fini(&c2_fop_iterator_test_fopt);
	c2_xc_iterator_test_xc_fini();
}

static int fop_init(void)
{
	c2_xc_iterator_test_xc_init();
	return C2_FOP_TYPE_INIT(&c2_fop_iterator_test_fopt,
				.name   = "FOP Iterator Test",
				.opcode = C2_FOP_ITERATOR_TEST_OPCODE,
				.xt     = c2_fop_iterator_test_xc);
}

static void fop_obj_fini(struct c2_fop_iterator_test *fop)
{
	c2_free(fop->fit_vec.fv_seg);
	c2_free(fop->fit_rec.fr_seq.fr_seq.fv_seg);
}

/* Just fill fop object fields */
static void fop_obj_init(struct c2_fop_iterator_test *fop)
{
	int i;

	fop->fit_fid.ff_seq = 1;
	fop->fit_fid.ff_oid = 2;

	fop->fit_vec.fv_count = 2;
	C2_ALLOC_ARR(fop->fit_vec.fv_seg, fop->fit_vec.fv_count);
	C2_UT_ASSERT(fop->fit_vec.fv_seg != NULL);
	for (i = 0; i < fop->fit_vec.fv_count; ++i) {
		fop->fit_vec.fv_seg[i].fs_count = i;
		fop->fit_vec.fv_seg[i].fs_offset = i*2;
	}

	fop->fit_opt0.fo_fid.ff_seq = 131;
	fop->fit_opt0.fo_fid.ff_oid = 132;

	fop->fit_opt1.fo_fid.ff_seq = 31;
	fop->fit_opt1.fo_fid.ff_oid = 32;

	fop->fit_topt.fo_fid.ff_seq = 41;
	fop->fit_topt.fo_fid.ff_oid = 42;

	fop->fit_rec.fr_fid.ff_seq = 5;
	fop->fit_rec.fr_fid.ff_oid = 6;
	fop->fit_rec.fr_seq.fr_fid.ff_seq = 7;
	fop->fit_rec.fr_seq.fr_fid.ff_oid = 8;
	fop->fit_rec.fr_seq.fr_seq.fv_count = 3;
	C2_ALLOC_ARR(fop->fit_rec.fr_seq.fr_seq.fv_seg,
		     fop->fit_rec.fr_seq.fr_seq.fv_count);
	C2_UT_ASSERT(fop->fit_rec.fr_seq.fr_seq.fv_seg != NULL);
	for (i = 0; i < fop->fit_rec.fr_seq.fr_seq.fv_count; ++i) {
		fop->fit_rec.fr_seq.fr_seq.fv_seg[i].fs_count = i;
		fop->fit_rec.fr_seq.fr_seq.fv_seg[i].fs_offset = i*2;
	}
	fop->fit_rec.fr_seq.fr_unn.fo_fid.ff_seq = 41;
	fop->fit_rec.fr_seq.fr_unn.fo_fid.ff_oid = 42;
}

static void fit_test(void)
{
	int                          result;
	int                          i = 0;
	struct c2_fop		    *f;
	struct c2_fid		    *fid;
	struct c2_fop_iterator_test *fop;
	struct c2_xcode_ctx          ctx;
	struct c2_xcode_cursor      *it;
	static struct c2_fid         expected[] = {
		{ 1,  2},   /* fop->fit_fid */
		{131, 132},
		{31, 32},   /* fop->fit_opt1.fo_fid */
		{41, 42},   /* fop->fit_topt.fo_fid */
		{ 5,  6},   /* fop->fit_rec.fr_fid */
		{ 7,  8},   /* fop->fit_rec.fr_seq.fr_fid */
                {41, 42}    /* fop->fit_rec.fr_seq.fr_unn.fo_fid */
	};

	result = fop_init();
	C2_UT_ASSERT(result == 0);

	f = c2_fop_alloc(&c2_fop_iterator_test_fopt, NULL);
	C2_UT_ASSERT(f != NULL);
	fop = c2_fop_data(f);
	fop_obj_init(fop);

	c2_xcode_ctx_init(&ctx, &C2_FOP_XCODE_OBJ(f));
	it = &ctx.xcx_it;

	while ((result = c2_xcode_next(it)) > 0) {
		const struct c2_xcode_type     *xt;
		struct c2_xcode_obj            *cur;
		struct c2_xcode_cursor_frame   *top;

		top = c2_xcode_cursor_top(it);

		if (top->s_flag != C2_XCODE_CURSOR_PRE)
			continue;

		cur = &top->s_obj;
		xt  = cur->xo_type;

		if (xt == c2_fop_fid_xc) {
			fid = cur->xo_ptr;
			C2_UT_ASSERT(fid->f_container ==
				     expected[i].f_container);
			C2_UT_ASSERT(fid->f_key ==
				     expected[i].f_key);
			++i;
		}
	}
	C2_UT_ASSERT(i == ARRAY_SIZE(expected));

	fop_obj_fini(fop);
	c2_fop_free(f);
	fop_fini();
}

const struct c2_test_suite fit_ut = {
	.ts_name = "fit-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "fop-iterator", fit_test },
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
