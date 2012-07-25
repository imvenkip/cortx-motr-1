/* -*- C -*- */
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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 08/17/2011
 */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/memory.h"		  /* C2_ALLOC_ARR */
#include "fop/fop.h"		  /* c2_fop */

#include "console/console.h"	  /* verbose */
#include "console/console_it.h"	  /* c2_fit */
#include "console/console_yaml.h" /* c2_cons_yaml_get_value */
#include "console/console_xc.h"

/**
   @addtogroup console_it
   @{
 */

static struct c2_cons_atom_ops atom_ops[C2_XAT_NR];

static void depth_print(int depth)
{
	static const char ruler[] = "\t\t\t\t\t.....\t";
	printf("%*.*s", depth, depth, ruler);
}

static void default_show(const struct c2_xcode_type *xct,
			 const char *name, void *data)
{
	printf("%s:%s\n", name, xct->xct_name);
}


static void void_get(const struct c2_xcode_type *xct,
		     const char *name, void *data)
{
}

static void void_set(const struct c2_xcode_type *xct,
		     const char *name, void *data)
{
}

static void byte_get(const struct c2_xcode_type *xct,
		     const char *name, void *data)
{
	char value = *(char *)data;

	if (verbose)
		printf("%s(%s) = %c\n", name, xct->xct_name, value);
}

static void byte_set(const struct c2_xcode_type *xct,
		     const char *name, void *data)
{
	void *tmp_value;
	char  value;

	if (yaml_support) {
		tmp_value = c2_cons_yaml_get_value(name);
		C2_ASSERT(tmp_value != NULL);
		*(char *)data = *(char *)tmp_value;
		if (verbose)
			printf("%s(%s) = %c\n", name, xct->xct_name,
			       *(char *)data);
	} else {
		printf("%s(%s) = ", name, xct->xct_name);
		if (scanf("\r%c", &value) != EOF)
			*(char *)data = value;
	}
}


static void u32_get(const struct c2_xcode_type *xct,
		    const char *name, void *data)
{
	uint32_t value = *(uint32_t *)data;

	if (verbose)
		printf("%s(%s) = %d\n", name, xct->xct_name, value);
}

static void u32_set(const struct c2_xcode_type *xct,
		    const char *name, void *data)
{
	uint32_t  value;
	void	 *tmp_value;

	if (yaml_support) {
		tmp_value = c2_cons_yaml_get_value(name);
		C2_ASSERT(tmp_value != NULL);
		*(uint32_t *)data = atoi((const char *)tmp_value);
		if (verbose)
			printf("%s(%s) = %u\n", name, xct->xct_name,
			       *(uint32_t *)data);
	} else {
		printf("%s(%s) = ", name, xct->xct_name);
		if (scanf("%u", &value) != EOF)
			*(uint32_t *)data = value;
	}
}

static void u64_get(const struct c2_xcode_type *xct,
		    const char *name, void *data)
{
	uint64_t value = *(uint64_t *)data;

	if (verbose)
		printf("%s(%s) = %ld\n", name, xct->xct_name, value);
}

static void u64_set(const struct c2_xcode_type *xct,
		    const char *name, void *data)
{
	void     *tmp_value;
	uint64_t  value;

	if (yaml_support) {
		tmp_value = c2_cons_yaml_get_value(name);
		C2_ASSERT(tmp_value != NULL);
		*(uint64_t *)data = atol((const char *)tmp_value);
		if (verbose)
			printf("%s(%s) = %ld\n", name, xct->xct_name,
			       *(uint64_t *)data);
	} else {
		printf("%s(%s) = ", name, xct->xct_name);
		if (scanf("%lu", &value) != EOF)
			*(uint64_t *)data = value;
	}
}


/**
 * @brief Methods to handle U64, U32 etc.
 */
static struct c2_cons_atom_ops atom_ops[C2_XAT_NR] = {
        [C2_XAT_VOID] = { void_get, void_set, default_show },
        [C2_XAT_U8]   = { byte_get, byte_set, default_show },
        [C2_XAT_U32]  = { u32_get, u32_set, default_show },
        [C2_XAT_U64]  = { u64_get, u64_set, default_show }
};

void c2_cons_fop_obj_input_output(struct c2_fop *fop,
				  enum c2_cons_data_process_type type)
{
	const struct c2_xcode_type  *xt;
	enum c2_xcode_aggr           gtype = 0;
	int                          fop_depth;
	int                          result;
	struct c2_xcode_ctx          ctx;
	struct c2_xcode_cursor      *it;

	fop_depth = 0;
	xt = *fop->f_type->ft_xc_type;
	C2_ASSERT(xt != NULL);
	c2_xcode_ctx_init(&ctx, &(struct c2_xcode_obj){xt, c2_fop_data(fop)});
	it = &ctx.xcx_it;

	printf("\n");

        while((result = c2_xcode_next(it)) > 0) {
		struct c2_xcode_obj                *cur;
		struct c2_xcode_cursor_frame       *top;
		const struct c2_xcode_cursor_frame *prev;
		const struct c2_xcode_type         *pt;
		const struct c2_xcode_obj          *par;
		size_t                              nob;
		size_t                              size;

		top = c2_xcode_cursor_top(it);

		if (top->s_flag == C2_XCODE_CURSOR_PRE) {

			cur   = &top->s_obj;
			xt    = cur->xo_type;
			gtype = xt->xct_aggr;
			if (gtype != C2_XA_ATOM && verbose) {
				++fop_depth;
				depth_print(fop_depth);
				printf("%s\n", xt->xct_name);
			} else if (gtype == C2_XA_ATOM) {
				enum c2_xode_atom_type atype = xt->xct_atype;

				++fop_depth;
				depth_print(fop_depth);
				size = xt->xct_sizeof;
				prev = top - 1;
				par  = &prev->s_obj;
				pt   = par->xo_type;
				switch (type) {
				case CONS_IT_INPUT:
					if (par->xo_type->xct_aggr ==
					    C2_XA_SEQUENCE) {
						nob = c2_xcode_tag(par) * size;
						cur->xo_ptr =
							c2_alloc(nob);
					}

					atom_ops[atype].catom_val_set(
					    xt,
					    pt->xct_child[
						prev->s_fieldno].xf_name,
					    cur->xo_ptr);
					break;
				case CONS_IT_OUTPUT:
					atom_ops[atype].catom_val_get(
					    xt,
					    pt->xct_child[
						    prev->s_fieldno].xf_name,
					    cur->xo_ptr);
					break;
				case CONS_IT_SHOW:
				default:
					atom_ops[atype].catom_val_show(
					    xt,
					    pt->xct_child[
						    prev->s_fieldno].xf_name,
					    cur->xo_ptr);
				}
			}
		} else if (top->s_flag == C2_XCODE_CURSOR_POST) {
			if (gtype != C2_XA_ATOM && verbose) {
				depth_print(fop_depth);
				printf("\n");
			}
			--fop_depth;
		}
        }
}

void c2_cons_fop_obj_input(struct c2_fop *fop)
{
	c2_cons_fop_obj_input_output(fop, CONS_IT_INPUT);
}

void c2_cons_fop_obj_output(struct c2_fop *fop)
{
	c2_cons_fop_obj_input_output(fop, CONS_IT_OUTPUT);
}

void c2_cons_fop_fields_show(struct c2_fop *fop)
{
	verbose = true;
	c2_cons_fop_obj_input_output(fop, CONS_IT_SHOW);
	verbose = false;
}

/** @} end of console_it group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

