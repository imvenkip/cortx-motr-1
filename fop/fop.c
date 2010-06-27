/* -*- C -*- */

#include "fop.h"
#include "lib/memory.h"

/**
   @addtogroup fop
   @{
 */

#if 0

static bool fop_field_subtree(struct c2_fop_field *root, unsigned depth,
			      c2_fop_field_cb_t pre_cb, 
			      c2_fop_field_cb_t post_cb, void *arg)
{
	struct c2_fop_field     *cur;
	enum c2_fop_field_cb_ret ret;

	ret = pre_cb(root, depth, arg);
	if (ret == FFC_CONTINUE) {
		do {
			c2_list_for_each_entry(&root->ff_child, cur, 
					       struct c2_fop_field, ff_sibling){
				if (!fop_field_subtree(cur, depth + 1, 
						       pre_cb, post_cb, arg))
					return false;
			}
			ret = post_cb(root, depth, arg);
		} while (ret == FFC_REPEAT);
	}
	return ret != FFC_BREAK;
}

void c2_fop_field_traverse(struct c2_fop_field *field,
			   c2_fop_field_cb_t pre_cb, 
			   c2_fop_field_cb_t post_cb, void *arg)
{
	fop_field_subtree(field, 0, pre_cb, post_cb, arg);
}
#endif

void c2_fop_field_type_unprepare(struct c2_fop_field_type *ftype);

void c2_fop_field_type_fini(struct c2_fop_field_type *t)
{
	size_t i;

	c2_fop_field_type_unprepare(t);
	if (t->fft_child != NULL) {
		for (i = 0; i < t->fft_nr; ++i) {
			if (t->fft_child[i] != NULL)
				c2_free(t->fft_child[i]);
		}
		c2_free(t->fft_child);
		t->fft_child = NULL;
	}
}

void c2_fop_field_type_map(const struct c2_fop_field_type *ftype,
			   c2_fop_field_cb_t cb, void *arg)
{
	size_t i;

	for (i = 0; i < ftype->fft_nr; ++i) {
		if (cb(ftype->fft_child[i], 1, arg) == FFC_BREAK)
			break;
	}
}

struct c2_fop_field_type C2_FOP_TYPE_VOID = {
	.fft_aggr = FFA_ATOM,
	.fft_name = "void",
	.fft_u = {
		.u_atom = {
			.a_type = FPF_VOID
		}
	}
};

struct c2_fop_field_type C2_FOP_TYPE_BYTE = {
	.fft_aggr = FFA_ATOM,
	.fft_name = "void",
	.fft_u = {
		.u_atom = {
			.a_type = FPF_BYTE
		}
	}
};


struct c2_fop_field_type C2_FOP_TYPE_U32 = {
	.fft_aggr = FFA_ATOM,
	.fft_name = "u32",
	.fft_u = {
		.u_atom = {
			.a_type = FPF_U32
		}
	}
};

struct c2_fop_field_type C2_FOP_TYPE_U64 = {
	.fft_aggr = FFA_ATOM,
	.fft_name = "u64",
	.fft_u = {
		.u_atom = {
			.a_type = FPF_U64
		}
	}
};

int  c2_fops_init(void)
{
	return 0;
}

void c2_fops_fini(void)
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
