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

void c2_fop_xcode_type_unprepare(struct c2_xcode_type *xct)
{
	int    i;
	size_t j;

	for (j = 0; j < ARRAY_SIZE(decorators); ++j) {
		if (xct->xct_decor[j] != NULL) {
			decorators[j]->dec_type_fini
				(xct->xct_decor[j]);
			c2_free(xct->xct_decor[j]);
		}
	}

	for (i = 0; i < xct->xct_nr; ++i) {
		struct c2_xcode_field *field;

		field = &xct->xct_child[i];
		for (j = 0; j < ARRAY_SIZE(decorators); ++j) {
			if (field->xf_decor[j] != NULL) {
				decorators[j]->dec_field_fini
					(field->xf_decor[j]);
				c2_free(field->xf_decor[j]);
			}
		}
	}
}

int c2_fop_xcode_type_prepare(struct c2_xcode_type *xct)
{
	return 0;
}

int c2_fop_type_xct_parse(struct c2_xcode_type *xct)
{
	int    rc;
	size_t nr;

	for (nr = 0; xct->xct_child[nr].xf_name != NULL; ++nr)
		C2_ASSERT(xct->xct_child[nr].xf_type != NULL);

	switch (xct->xct_aggr) {
	case C2_XA_RECORD:
		C2_ASSERT(nr > 0);
		break;
	case C2_XA_UNION:
		C2_ASSERT(nr > 1);
		break;
	case C2_XA_SEQUENCE:
		C2_ASSERT(nr == 2);
		break;
	case C2_XA_TYPEDEF:
		C2_ASSERT(nr == 1);
		break;
	case C2_XA_ATOM:
		C2_ASSERT(nr == 0);
		break;
	default:
		C2_IMPOSSIBLE("Invalid xcode type aggregate.");
	}

	/* XXX: add sanity checking:

	       - tags and field names are unique;
	       - discriminant is U32
	*/

	rc = c2_fop_xcode_type_prepare(xct);
	if (rc == 0) {
		rc = c2_fop_xct_fit(xct);
		if (rc != 0)
			c2_fop_type_xct_fini(xct);
	}

	return rc;
}

void *c2_fop_type_decoration_get(const struct c2_xcode_type *xct,
				 const struct c2_fop_decorator *dec)
{
	decoration_used = true;
	return xct->xct_decor[dec->dec_id];
}

void c2_fop_type_decoration_set(struct c2_xcode_type *xct,
				const struct c2_fop_decorator *dec, void *val)
{
	decoration_used = true;
	xct->xct_decor[dec->dec_id] = val;
}

void *c2_fop_field_decoration_get(const struct c2_xcode_field   *field,
				  const struct c2_fop_decorator *dec)
{
	decoration_used = true;
	return field->xf_decor[dec->dec_id];
}

void  c2_fop_field_decoration_set(struct c2_xcode_field *field,
				  const struct c2_fop_decorator *dec,
				  void *val)
{
	decoration_used = true;
	field->xf_decor[dec->dec_id] = val;
}

void c2_fop_decorator_register(struct c2_fop_decorator *dec)
{
	C2_PRE(decorators_nr < ARRAY_SIZE(decorators));
	C2_PRE(!decoration_used);

	decorators[decorators_nr] = dec;
	dec->dec_id = decorators_nr++;
}

void c2_fop_type_xct_fini(struct c2_xcode_type *xct)
{
	c2_fop_field_type_fini(xct);
}

int c2_fop_type_xct_parse_nr(struct c2_xcode_type **xct, int nr)
{
	int i;
	int result;

	for (result = 0, i = 0; i < nr; ++i) {
		result = c2_fop_type_xct_parse(xct[i]);
		if (result != 0) {
			c2_fop_type_xct_fini_nr(xct, i);
			break;
		}
	}
	return result;
}
C2_EXPORTED(c2_fop_type_xct_parse_nr);

void c2_fop_type_xct_fini_nr(struct c2_xcode_type **xct, int nr)
{
	int i;

	for (i = 0; i < nr; ++i)
		c2_fop_type_xct_fini(xct[i]);
}

struct c2_xcode_field *
c2_fop_type_field_find(struct c2_xcode_type *xct, const char *fname)
{
	size_t i;

	for (i = 0; i < xct->xct_nr; ++i) {
		if (!strcmp(fname, xct->xct_child[i].xf_name))
			return &xct->xct_child[i];
	}
	return NULL;
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
