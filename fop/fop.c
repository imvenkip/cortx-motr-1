/* -*- C -*- */

#include "fop.h"
#include "lib/memory.h"

/**
   @addtogroup fop
   @{
 */

struct c2_fop_field *c2_fop_field_alloc(void)
{
	struct c2_fop_field *field;

	C2_ALLOC_PTR(field);
	if (field != NULL) {
		c2_list_link_init(&field->ff_sibling);
		c2_list_init(&field->ff_child);
	}
	return field;
}

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

static void fop_field_fini_one(struct c2_fop_field *fop) 
{
	c2_list_del_init(&fop->ff_sibling);
	c2_list_fini(&fop->ff_child);
	c2_free(fop);
}

static struct c2_fop_field *list2field(struct c2_list_link *link)
{
	return container_of(link, struct c2_fop_field, ff_sibling);
}

void c2_fop_field_fini(struct c2_fop_field *field)
{
	struct c2_fop_field *scan;
	struct c2_fop_field *next;

	while (1) {
		while (!c2_list_is_empty(&scan->ff_child))
			scan = list2field(scan->ff_child.first);
		next = scan->ff_parent;
		fop_field_fini_one(scan);
		if (scan == field)
			break;
		scan = next;
	}
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
