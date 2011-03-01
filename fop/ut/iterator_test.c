#ifdef HAVE_CONFIG_H
#  include <config.h>
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


/* FOP object iterator test tests iterations of the following types:
 *   - FFA_TYPEDEF,
 *   - FFA_RECORD,
 *   - FFA_UNION,
 *   - FFA_SEQUENCE.
 */

C2_FOP_TYPE_DECLARE(c2_fop_iterator_test, "FOP iterator test", 0, NULL);

static struct c2_fop_type *fops[] = {
	&c2_fop_iterator_test_fopt,
};

static struct c2_fop_type_format *fmts[] = {
	&c2_fop_fid_tfmt,
	&c2_fop_fid_tfmt,
	&c2_fop_seg_tfmt,
	&c2_fop_vec_tfmt,
	&c2_fop_optfid_tfmt,
	&c2_fop_fid_typedef_tfmt,
	&c2_fop_recursive1_tfmt,
	&c2_fop_recursive2_tfmt
};

static void fop_fini(void)
{
	c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
	c2_fop_type_format_fini_nr(fmts, ARRAY_SIZE(fmts));
}

static int fop_init(void)
{
	int result;

	result = c2_fop_type_format_parse_nr(fmts, ARRAY_SIZE(fmts));
	if (result == 0) {
		result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
		if (result == 0)
			c2_fop_object_init(&c2_fop_fid_tfmt);
	}
	if (result != 0)
		fop_fini();
	return result;
}

/* Just fill fop object fields */
static void fop_obj_init(struct c2_fop_iterator_test *fop)
{
	int i;

	fop->fit_fid.ff_oid = 1;
	fop->fit_fid.ff_seq = 2;
	
	fop->fit_vec.fv_count = 2;
	C2_ALLOC_ARR(fop->fit_vec.fv_seg, fop->fit_vec.fv_count);
	C2_UT_ASSERT(fop->fit_vec.fv_seg != NULL);
	for (i = 0; i < fop->fit_vec.fv_count; ++i) {
		fop->fit_vec.fv_seg[i].fs_count = i;
		fop->fit_vec.fv_seg[i].fs_offset = i*2;
	}

	fop->fit_opt.fo_present = 3;
	fop->fit_opt.u.fo_fid.ff_oid = 31;
	fop->fit_opt.u.fo_fid.ff_seq = 32;

	fop->fit_topt.fo_present = 4;
	fop->fit_topt.u.fo_fid.ff_oid = 41;
	fop->fit_topt.u.fo_fid.ff_seq = 42;

	fop->fit_rec.fr_fid.ff_oid = 5;
	fop->fit_rec.fr_fid.ff_seq = 6;
	fop->fit_rec.fr_seq.fr_fid.ff_oid = 7;
	fop->fit_rec.fr_seq.fr_fid.ff_seq = 8;
	fop->fit_rec.fr_seq.fr_seq.fv_count = 3;
	C2_ALLOC_ARR(fop->fit_rec.fr_seq.fr_seq.fv_seg,
		     fop->fit_rec.fr_seq.fr_seq.fv_count);
	C2_UT_ASSERT(fop->fit_rec.fr_seq.fr_seq.fv_seg != NULL);
	for (i = 0; i < fop->fit_rec.fr_seq.fr_seq.fv_count; ++i) {
		fop->fit_rec.fr_seq.fr_seq.fv_seg[i].fs_count = i;
		fop->fit_rec.fr_seq.fr_seq.fv_seg[i].fs_offset = i*2;
	}
	fop->fit_rec.fr_seq.fr_unn.fo_present = 5;
	fop->fit_rec.fr_seq.fr_unn.u.fo_fid.ff_oid = 41;
	fop->fit_rec.fr_seq.fr_unn.u.fo_fid.ff_seq = 42;
}

/* FOP object iterator test function */
static void fit_test(void)
{
	int result;
	int i = 0;
	uint64_t bits = 0;
	struct c2_fop			*f;
	struct c2_fit			 it;
	struct c2_fid			*fid = NULL;
	struct c2_fop_iterator_test	*fop;
	static struct c2_fid expected[] = {
		{1, 2},
		{3, 4},
		{5, 6}
	};


	result = fop_init();
	C2_UT_ASSERT(result == 0);


	f = c2_fop_alloc(&c2_fop_iterator_test_fopt, NULL);
	C2_UT_ASSERT(f != NULL);
	fop = c2_fop_data(f);
	fop_obj_init(fop);


	c2_fop_object_it_init(&it, f);

	while ((result = c2_fop_object_it_yield(&it, fid, &bits)) > 0) {
		C2_UT_ASSERT(fid->f_container == expected[i].f_container);
		C2_UT_ASSERT(fid->f_key == expected[i].f_key);
	}
	C2_UT_ASSERT(result == ARRAY_SIZE(expected));

	c2_fop_object_it_fini(&it);


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
