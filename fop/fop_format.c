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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 05/19/2010
 */

#include "lib/errno.h"
#include "lib/cdefs.h"
#include "lib/assert.h"
#include "lib/memory.h"

#include "fop/fop_base.h"
#include "fop/fop_iterator.h"

/**
   @addtogroup fop
   @{
 */

enum {
	C2_FOP_DECORATOR_MAX = 4
};

static struct c2_fop_decorator *decorators[C2_FOP_DECORATOR_MAX];
static size_t decorators_nr = 0;
static bool decoration_used = false;

void c2_fop_field_type_unprepare(struct c2_fop_field_type *ftype)
{
	int    i;
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
				if (field->ff_decor[j] != NULL)
					decorators[j]->dec_field_fini
						(field->ff_decor[j]);
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
	if (result != 0)
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
		C2_ASSERT(nr == 2);
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

	if (result == 0) {
		result = c2_fop_field_type_prepare(t);
		if (result == 0)
			result = c2_fop_field_type_fit(t);
	}

	if (result == 0) {
		fmt->ftf_out = t;
		t->fft_layout = fmt->ftf_layout;
	} else
		c2_fop_type_format_fini(fmt);
	return result;
}

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

void c2_fop_type_format_fini(struct c2_fop_type_format *fmt)
{
	if (fmt->ftf_out != NULL) {
		c2_fop_field_type_fini(fmt->ftf_out);
		c2_free(fmt->ftf_out);
		fmt->ftf_out = NULL;
	}
}

int c2_fop_type_format_parse_nr(struct c2_fop_type_format **fmt, int nr)
{
	int i;
	int result;

	for (result = 0, i = 0; i < nr; ++i) {
		result = c2_fop_type_format_parse(fmt[i]);
		if (result != 0) {
			c2_fop_type_format_fini_nr(fmt, i);
			break;
		}
	}
	return result;
}
C2_EXPORTED(c2_fop_type_format_parse_nr);

void c2_fop_type_format_fini_nr(struct c2_fop_type_format **fmt, int nr)
{
	int i;

	for (i = 0; i < nr; ++i)
		c2_fop_type_format_fini(fmt[i]);
}

void *c2_fop_type_field_addr(const struct c2_fop_field_type *ftype, void *obj,
			     int fileno, uint32_t elno)
{
	void *addr;

	C2_ASSERT(fileno < ftype->fft_nr);
	addr = ((char *)obj) + ftype->fft_layout->fm_child[fileno].ch_offset;
	if (ftype->fft_aggr == FFA_SEQUENCE && fileno == 1 && elno != ~0)
		addr = *((char **)addr) + elno *
			ftype->fft_child[1]->ff_type->fft_layout->fm_sizeof;
	return addr;
}

struct c2_fop_field *
c2_fop_type_field_find(const struct c2_fop_field_type *ftype, const char *fname)
{
	size_t i;

	for (i = 0; i < ftype->fft_nr; ++i) {
		if (!strcmp(fname, ftype->fft_child[i]->ff_name))
			return ftype->fft_child[i];
	}
	return NULL;
}

const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_VOID_tfmt = {
	.ftf_out   = &C2_FOP_TYPE_VOID,
	.ftf_aggr  = FFA_ATOM,
	.ftf_name  = "void",
	.ftf_val   = FPF_VOID,
	.ftf_child = { [0] = { .c_name = NULL } }
};

const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_BYTE_tfmt = {
	.ftf_out   = &C2_FOP_TYPE_BYTE,
	.ftf_aggr  = FFA_ATOM,
	.ftf_name  = "byte",
	.ftf_val   = FPF_BYTE,
	.ftf_child = { [0] = { .c_name = NULL } }
};

const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_U32_tfmt = {
	.ftf_out   = &C2_FOP_TYPE_U32,
	.ftf_aggr  = FFA_ATOM,
	.ftf_name  = "u32",
	.ftf_val   = FPF_U32,
	.ftf_child = { [0] = { .c_name = NULL } }
};
C2_EXPORTED(C2_FOP_TYPE_FORMAT_U32_tfmt);

const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_U64_tfmt = {
	.ftf_out   = &C2_FOP_TYPE_U64,
	.ftf_aggr  = FFA_ATOM,
	.ftf_name  = "u64",
	.ftf_val   = FPF_U64,
	.ftf_child = { [0] = { .c_name = NULL } }
};
C2_EXPORTED(C2_FOP_TYPE_FORMAT_U64_tfmt);

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
