/* -*- C -*- */

#include <errno.h>

#include "lib/assert.h"
#include "lib/memory.h"

#include "fop_format.h"

/**
   @addtogroup fop
   @{
 */

int c2_fop_type_format_parse(struct c2_fop_type_format *fmt)
{
	struct c2_fop_field_type *t;
	int    result;
	size_t nr;
	size_t i;


	C2_PRE(fmt->ftf_out == NULL);

	C2_ALLOC_PTR(t);
	if (t == NULL)
		return -ENOMEM;
	for (nr = 0; fmt->ftf_child[nr].c_name != NULL; ++nr)
		C2_ASSERT(fmt->ftf_child[nr].c_type != NULL);

	t->fft_aggr = fmt->ftf_aggr;
	t->fft_name = fmt->ftf_name;
	t->fft_nr   = nr;

	switch (fmt->ftf_aggr) {
	case FFA_RECORD:
		C2_ASSERT(nr > 0);
		break;
	case FFA_UNION:
		C2_ASSERT(nr > 1);
		break;
	case FFA_SEQUENCE:
		C2_ASSERT(fmt->ftf_val > 0);
		C2_ASSERT(nr == 1);
		t->fft_u.u_sequence.s_max = fmt->ftf_val;
		break;
	case FFA_TYPEDEF:
		C2_ASSERT(nr == 1);
		break;
	case FFA_ATOM:
		C2_ASSERT(nr == 0);
		t->fft_u.u_atom.a_type = fmt->ftf_val;
		C2_ASSERT(0 <= fmt->ftf_val && fmt->ftf_val < FPF_NR);
		break;
	default:
		C2_IMPOSSIBLE("Invalid fop type aggregate.");
	}

	result = 0;
	if (nr > 0) {
		C2_ALLOC_ARR(t->fft_child, nr);
		if (t->fft_child != NULL) {
			for (i = 0; i < nr; ++i) {
				struct c2_fop_field              *field;
				const struct c2_fop_field_format *field_fmt;

				C2_ALLOC_PTR(t->fft_child[i]);
				field = t->fft_child[i];
				if (field == NULL) {
					result = -ENOMEM;
					break;
				}
				field_fmt = &fmt->ftf_child[i];
				field->ff_name = field_fmt->c_name;
				field->ff_type = field_fmt->c_type->ftf_out;
				field->ff_tag  = field_fmt->c_tag;
			}
		}
	}

	/* XXX: add sanity checking: 

	       - tags and field names are unique;
	       - discriminant is U32
	*/

	if (result == 0)
		fmt->ftf_out = t;
	else
		c2_fop_field_type_fini(t);
	return result;
}

const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_VOID = {
	.ftf_out   = &C2_FOP_TYPE_VOID,
	.ftf_aggr  = FFA_ATOM,
	.ftf_name  = "void",
	.ftf_val   = FPF_VOID,
	.ftf_child = { [0] = { .c_name = NULL } }
};

const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_BYTE = {
	.ftf_out   = &C2_FOP_TYPE_BYTE,
	.ftf_aggr  = FFA_ATOM,
	.ftf_name  = "byte",
	.ftf_val   = FPF_BYTE,
	.ftf_child = { [0] = { .c_name = NULL } }
};

const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_U32 = {
	.ftf_out   = &C2_FOP_TYPE_U32,
	.ftf_aggr  = FFA_ATOM,
	.ftf_name  = "u32",
	.ftf_val   = FPF_U32,
	.ftf_child = { [0] = { .c_name = NULL } }
};

const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_U64 = {
	.ftf_out   = &C2_FOP_TYPE_U64,
	.ftf_aggr  = FFA_ATOM,
	.ftf_name  = "u64",
	.ftf_val   = FPF_U64,
	.ftf_child = { [0] = { .c_name = NULL } }
};

/** @} end of fop group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
