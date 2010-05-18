/* -*- C -*- */

#include "fop_format.h"
#include "lib/assert.h"

/**
   @addtogroup fop
   @{
 */

const struct c2_fop_field c2_fop_write[] = {
	C2_FOP_FIELD_FORMAT("", FFT_RECORD),
		C2_FOP_FIELD_FORMAT("wr_fid", FFT_FID),
		C2_FOP_FIELD_FORMAT("wr_iovec", FFT_ARRAY),
       			C2_FOP_FIELD_FORMAT("iv_offset", FFT_OFFSET),
			C2_FOP_FIELD_FORMAT("iv_buf", FFT_BUFFER),
        	C2_FOP_FORMAT_END,
	C2_FOP_FORMAT_END
};

int c2_fop_field_format_parse(const struct c2_fop_field_format *format, 
			      size_t nr, struct c2_fop_field **out)
{
	struct c2_fop_field *parent;
	struct c2_fop_field *top;
	int      result;
	unsigned depth;
	size_t   i;

	parent = NULL;
	depth = 0;

	for (i = 0; i < nr; ++i) {
		struct c2_fop_field        *cur;
		struct c2_fop_field_format *fmt;

		fmt = &format[i];
		if (fmt->fif_name == NULL) {
			C2_ASSERT(fmt->fif_type == FFT_ZERO);
			C2_ASSERT(depth > 0);
			C2_ASSERT(parent != NULL);
			depth--;
			parent = parent->ff_parent;
			C2_ASSERT((depth == 0) == (parent == NULL));
			C2_ASSERT(ergo(depth == 0, i + 1 == nr)); 
			continue;
		}
		cur = c2_fop_field_alloc();
		if (cur == NULL) {
			result = -ENOMEM;
			break;
		} else if (top == NULL)
			top = cur;
		cur->ff_name = fmt->fif_name;
		C2_ASSERT(0 <= fmt->fif_type);
		C2_ASSERT(fmt->fif_type < ARRAY_SIZE(fop_field_base));
		cur->ff_base = &fop_field_base[fmt->fif_type];
		if (parent != NULL)
			c2_list_add_tail(&parent->ff_child, &cur->ff_sibling);
		cur->ff_parent = parent;
		if (cur->ff_base->ffb_compound) {
			parent = cur;
			depth++;
			C2_ASSERT(depth < C2_FOP_MAX_FIELD_DEPTH);
		}
	}
	if (result == 0) {
		C2_ASSERT(depth == 0);
		C2_ASSERT(i == nr);
		C2_ASSERT(top != NULL);
		*out = top;
	} else
		c2_fop_field_fini(top);
	return result;
}

static const struct fop_field_base c2_fop_field_bases[FFT_NR];


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
