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
#include "fop/fop_iterator.h"
#include "iterator_test_u.h"
#include "fop/fop_format_def.h"
#include "fop/ut/iterator_test.ff"
#include "rpc/rpc_opcodes.h"


/* FOP object iterator test tests iterations of the following types:
 *   - FFA_TYPEDEF,
 *   - FFA_RECORD,
 *   - FFA_UNION,
 *   - FFA_SEQUENCE.
 */

C2_FOP_TYPE_DECLARE(c2_fop_iterator_test, "FOP iterator test", NULL,
		    C2_FOP_ITERATOR_TEST_OPCODE, 0);

static struct c2_fop_type *fops[] = {
	&c2_fop_iterator_test_fopt,
};

/*
 * Split fop formats in two groups, because c2_fop_object_init() has to be
 * called after c2_fop_fid_tfmt has been parsed, but before other types having
 * fids in them have been.
 */

static struct c2_fop_type_format *fmts0[] = {
	&c2_fop_fid_tfmt
};

static struct c2_fop_type_format *fmts[] = {
	&c2_fop_seg_tfmt,
	&c2_fop_vec_tfmt,
	&c2_fop_optfid_tfmt,
	&c2_fop_fid_typedef_tfmt,
	&c2_fop_recursive1_tfmt,
	&c2_fop_recursive2_tfmt
};

static void fop_fini(void)
{
	c2_fop_object_fini();
	c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
	c2_fop_type_format_fini_nr(fmts, ARRAY_SIZE(fmts));
	c2_fop_type_format_fini_nr(fmts0, ARRAY_SIZE(fmts0));
}

static int fop_init(void)
{
	int result;

	result = c2_fop_type_format_parse_nr(fmts0, ARRAY_SIZE(fmts0));
	C2_UT_ASSERT(result == 0);
	c2_fop_object_init(&c2_fop_fid_tfmt);
	result = c2_fop_type_format_parse_nr(fmts, ARRAY_SIZE(fmts));
	C2_UT_ASSERT(result == 0);
	result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
	C2_UT_ASSERT(result == 0);
	return result;
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

	fop->fit_opt0.fo_present = 0; /* void */
	fop->fit_opt0.u.fo_fid.ff_seq = 131;
	fop->fit_opt0.u.fo_fid.ff_oid = 132;

	fop->fit_opt1.fo_present = 1;
	fop->fit_opt1.u.fo_fid.ff_seq = 31;
	fop->fit_opt1.u.fo_fid.ff_oid = 32;

	fop->fit_topt.fo_present = 1;
	fop->fit_topt.u.fo_fid.ff_seq = 41;
	fop->fit_topt.u.fo_fid.ff_oid = 42;

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
	fop->fit_rec.fr_seq.fr_unn.fo_present = 1;
	fop->fit_rec.fr_seq.fr_unn.u.fo_fid.ff_seq = 41;
	fop->fit_rec.fr_seq.fr_unn.u.fo_fid.ff_oid = 42;
}

/**
   FOP object iterator test function

   @todo add FFA_SEQUENCE tests.
 */
static void fit_test(void)
{
	int result;
	int i = 0;
	uint64_t bits = 0;
	struct c2_fop			*f;
	struct c2_fit			 it;
	struct c2_fid			 fid;
	struct c2_fop_iterator_test	*fop;
	static struct c2_fid expected[] = {
		{ 1,  2},   /* fop->fit_fid */
		{31, 32},   /* fop->fit_opt1.u.fo_fid */
		{41, 42},   /* fop->fit_topt.u.fo_fid */
		{ 5,  6},   /* fop->fit_rec.fr_fid */
		{ 7,  8},   /* fop->fit_rec.fr_seq.fr_fid */
                {41, 42}    /* fop->fit_rec.fr_seq.fr_unn.u.fo_fid */
	};


	result = fop_init();
	C2_UT_ASSERT(result == 0);


	f = c2_fop_alloc(&c2_fop_iterator_test_fopt, NULL);
	C2_UT_ASSERT(f != NULL);
	fop = c2_fop_data(f);
	fop_obj_init(fop);


	c2_fop_object_it_init(&it, f);

	for (; (result = c2_fop_object_it_yield(&it, &fid, &bits)) > 0; i++) {
		C2_UT_ASSERT(fid.f_container == expected[i].f_container);
		C2_UT_ASSERT(fid.f_key == expected[i].f_key);
	}
	C2_UT_ASSERT(i == ARRAY_SIZE(expected));

	c2_fop_object_it_fini(&it);

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
