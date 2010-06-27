/* -*- C -*- */

#include <errno.h>

#include "lib/assert.h"
#include "lib/memory.h"

#include "fop_format.h"

/**
   @addtogroup fop
   @{
 */

void c2_fop_field_type_unprepare(struct c2_fop_field_type *ftype)
{
	int    i
	size_t j;

	if (ftype->fft_decor != NULL) {
		for (j = 0; j < ARRAY_SIZE(decorators); ++j) {
			if (ftype->fft_decor[j] != NULL)
				decorators[j]->dec_type_fini
					(ftype->fft_decor[j]);
		}
		c2_free(ftype->fft_decor);
		ftype->fft_decor = NULL;
	}
	for (i = 0; i < ftype->fft_nr; ++i) {
		struct c2_fop_field *field;

		field = ftype->fft_child[i];
		if (field->ff_decor != NULL) {
			for (j = 0; j < ARRAY_SIZE(decorators); ++j) {
				if (field->ff_decor[i] != NULL)
					decorators[i]->dec_field_fini
						(field->ff_decor[i]);
			}
		}
		c2_free(field->ff_decor);
		field->ff_decor = NULL;
	}
}

int c2_fop_field_type_prepare(struct c2_fop_field_type *ftype)
{
	int    result;
	size_t i;

	C2_PRE(ftype->fft_decor == NULL);

	C2_ALLOC_ARR(ftype->fft_decor, ARRAY_SIZE(decorators));
	if (ftype->fft_decor != NULL) {
		result = 0;
		for (i = 0; i < ftype->fft_nr; ++i) {
			struct c2_fop_field *field;

			field = ftype->fft_child[i];
			C2_PRE(field->ff_decor == NULL);
			C2_PRE(field->ff_type->fft_decor != NULL);

			C2_ALLOC_ARR(field->ff_decor, ARRAY_SIZE(decorators));
			if (field->ff_decor == NULL) {
				result = -ENOMEM;
				break;
			}
		}
	} else
		result = -ENOMEM;
	if (result != NULL)
		c2_fop_field_type_unprepare(ftype);
	return result;
}

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
		result = c2_fop_field_type_prepare(t);

	if (result == 0)
		fmt->ftf_out = t;
	else
		c2_fop_field_type_fini(t);
	return result;
}

enum {
	C2_FOP_DECORATOR_MAX = 4
};

static struct c2_fop_decorator *decorators[C2_FOP_DECORATOR_MAX];
static size_t decorators_nr = 0;
static bool decoration_used = false;

void *c2_fop_type_decoration_get(const struct c2_fop_field_type *ftype,
				 const struct c2_fop_decorator *dec)
{
	decoration_used = true;
	return ftype->fft_decor[dec->dec_id];
}

void  c2_fop_type_decoration_set(const struct c2_fop_field_type *ftype,
				 const struct c2_fop_decorator *dec, void *val)
{
	decoration_used = true;
	ftype->fft_decor[dec->dec_id] = val;
}

void *c2_fop_field_decoration_get(const struct c2_fop_field *field,
				  const struct c2_fop_decorator *dec)
{
	decoration_used = true;
	return field->ff_decor[dec->dec_id];
}

void  c2_fop_field_decoration_set(const struct c2_fop_field *field,
				  const struct c2_fop_decorator *dec,
				  void *val)
{
	decoration_used = true;
	field->ff_decor[dec->dec_id] = val;
}

void c2_fop_decorator_register(struct c2_fop_decorator *dec)
{
	C2_PRE(decorators_nr < ARRAY_SIZE(decorators));
	C2_PRE(!decoration_used);

	decorators[decorators_nr] = dec;
	dec->dec_id = decorators_nr++;
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
