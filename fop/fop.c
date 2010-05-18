/* -*- C -*- */

#include "fop.h"
#include "lib/assert.h"

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

#if 0
void c2_fop_field_traverse(struct c2_fop_field *fop, 
			   c2_fop_field_cb_t cb, void *arg);
{
	struct c2_fop_field *cur;
	struct c2_fop_field *path[C2_FOP_MAX_FIELD_DEPTH];
	int                  depth;

	depth   = 0;
	path[0] = fop;
	while (1) {
		cur = path[depth];
		if (pre && !cb(cur, depth, arg))
			break;
		else if (!c2_list_is_empty(&cur->ff_child))
			path[++depth] = list2field(cur->ff_child.first);
		else if (cur->ff_sibling.next != &cur->ff_parent.ff_child)
			path[depth] = list2field(cur->ff_sibling.next);
		else if ((!pre && !cb(cur, depth, arg) || --depth < 0))
			break;
	}
}
#endif

static void fop_field_fini_one(struct c2_fop_field *fop) 
{
	c2_list_del_init(&fop->ff_sibling);
	c2_list_fini(&fop->ff_child);
	c2_free(fop);
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
