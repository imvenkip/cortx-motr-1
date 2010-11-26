/* -*- C -*- */

#include "lib/assert.h"
#include "fop/fop_iterator.h"

/**
   @addtogroup fop

   <b>Fop iterator implementation.</b>

   @{
 */

enum {
	FIT_TYPE_MAX = 16
};

static struct c2_fit_type *fits[FIT_TYPE_MAX] = { NULL, };

void c2_fop_itype_init(struct c2_fit_type *itype)
{
	int i;

	C2_PRE(itype->fit_index == 0);

	for (i = 0; i < ARRAY_SIZE(fits); ++i) {
		if (fits[i] == NULL) {
			fits[i] = itype;
			itype->fit_index = i;
			break;
		}
	}
	C2_ASSERT(i < ARRAY_SIZE(fits));
	/* initialise the fields... */
}

void c2_fop_itype_fini(struct c2_fit_type *itype)
{
	/* finalise the fields... */
	fits[itype->fit_index] = NULL;
	itype->fit_index = 0;
}

void c2_fop_itype_add(struct c2_fit_type *itype, struct c2_fit_watch *watch)
{
	c2_list_add(&itype->fit_watch, &watch->fif_linkage);
}

void c2_fop_itype_mod(struct c2_fit_type *itype, struct c2_fit_mod *mod)
{
	/* check the pre-condition... */
	c2_list_add(&itype->fit_mod, &mod->fm_linkage);
}

void c2_fit_init(struct c2_fit *it, struct c2_fit_type *itype, 
		 struct c2_fop *fop)
{
	it->fi_type = itype;
	it->fi_fop  = fop;
}

void c2_fit_fini(struct c2_fit *it)
{
}

int c2_fit_yield(struct c2_fit *it, struct c2_fit_yield *yield)
{
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
