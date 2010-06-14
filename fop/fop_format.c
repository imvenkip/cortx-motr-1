/* -*- C -*- */

#include <errno.h>

#include "fop_format.h"
#include "lib/assert.h"

/**
   @addtogroup fop
   @{
 */

#define BASE(type, compound) [type] = {		\
	.ffb_kind = FFK_OTHER,			\
	.ffb_type = (type),			\
	.ffb_compound = (compound)		\
}

const struct c2_fop_field_base c2_fop_field_base[FFT_NR] = {
	BASE(FFT_ZERO, false),
	BASE(FFT_VOID, false),
	BASE(FFT_BOOL, false),
	BASE(FFT_CHAR, false),
	BASE(FFT_64,   false),
	BASE(FFT_32,   false),
	BASE(FFT_RECORD, true),
	BASE(FFT_UNION, true),
	BASE(FFT_ARRAY, true),
	BASE(FFT_BITMASK, false),
	BASE(FFT_FID, false),
	BASE(FFT_NAME, false),
	BASE(FFT_PATH, false),
	BASE(FFT_PRINCIPAL, false),
	BASE(FFT_CAPABILITY, false),
	BASE(FFT_TIMESTAMP, false),
	BASE(FFT_EPOCH, false),
	BASE(FFT_VERSION, false),
	BASE(FFT_OFFSET, false),
	BASE(FFT_COUNT, false),
	BASE(FFT_BUFFER, false),
	BASE(FFT_RESOURCE, false),
	BASE(FFT_LOCK, false),
	BASE(FFT_NODE, false),
	BASE(FFT_FOP, false),
	BASE(FFT_REF, false),
	BASE(FFT_OTHER, false)
};

int c2_fop_field_format_parse(struct c2_fop_field_descr *descr)
{
	struct c2_fop_field *parent;
	struct c2_fop_field *top;
	int      result;
	unsigned depth;
	size_t   i;

	parent = NULL;
	top    = NULL;
	depth  = 0;
	result = 0;

	for (i = 0; i < descr->ffd_nr; ++i) {
		struct c2_fop_field              *cur;
		const struct c2_fop_field_format *fmt;

		C2_ASSERT((parent == NULL) == (i == 0));

		fmt = &descr->ffd_fmt[i];
		if (fmt->fif_name == NULL) {
			C2_ASSERT(fmt->fif_base == NULL);
			C2_ASSERT(depth > 0);
			C2_ASSERT(parent != NULL);
			depth--;
			parent = parent->ff_parent;
			C2_ASSERT((depth == 0) == (parent == NULL));
			C2_ASSERT((depth == 0) == (i + 1 == descr->ffd_nr)); 
			continue;
		}
		cur = c2_fop_field_alloc();
		if (cur == NULL) {
			result = -ENOMEM;
			break;
		} else if (top == NULL)
			top = cur;
		cur->ff_name = fmt->fif_name;
		if (fmt->fif_ref != NULL)
			cur->ff_ref  = fmt->fif_ref->ffd_field;
		cur->ff_base = fmt->fif_base;
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
		C2_ASSERT(i == descr->ffd_nr);
		C2_ASSERT(top != NULL);
		descr->ffd_field = top;
	} else
		c2_fop_field_fini(top);
	return result;
}


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
