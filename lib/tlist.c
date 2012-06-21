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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/26/2011
 */

#include "lib/tlist.h"

/**
   @addtogroup tlist
   @{
 */

/**
   Returns the address of a link embedded in an ambient object.
 */
static struct c2_list_link *link(const struct c2_tl_descr *d, const void *obj);

/**
   Returns the value of the magic field in an ambient object
 */
static uint64_t magic(const struct c2_tl_descr *d, const void *obj);

/**
   Casts a link to its ambient object.
 */
static void *amb(const struct c2_tl_descr *d, struct c2_list_link *link);

void c2_tlist_init(const struct c2_tl_descr *d, struct c2_tl *list)
{
	list->t_magic = d->td_head_magic;
	c2_list_init(&list->t_head);
	C2_POST(c2_tlist_invariant(d, list));
}

void c2_tlist_fini(const struct c2_tl_descr *d, struct c2_tl *list)
{
	C2_PRE(c2_tlist_invariant(d, list));
	c2_list_fini(&list->t_head);
}

void c2_tlink_init(const struct c2_tl_descr *d, void *obj)
{
	c2_list_link_init(link(d, obj));
	if (d->td_link_magic != 0)
		*(uint64_t *)(obj + d->td_link_magic_offset) = d->td_link_magic;
	C2_POST(c2_tlink_invariant(d, obj));
}

void c2_tlink_init_at(const struct c2_tl_descr *d, void *obj,
		      struct c2_tl *list)
{
	c2_tlink_init(d, obj);
	c2_tlist_add(d, list, obj);
}

void c2_tlink_init_at_tail(const struct c2_tl_descr *d, void *obj,
			   struct c2_tl *list)
{
	c2_tlink_init(d, obj);
	c2_tlist_add_tail(d, list, obj);
}

void c2_tlink_fini(const struct c2_tl_descr *d, void *obj)
{
	c2_list_link_fini(link(d, obj));
}

void c2_tlink_del_fini(const struct c2_tl_descr *d, void *obj)
{
	c2_tlist_del(d, obj);
	c2_tlink_fini(d, obj);
}

bool c2_tlist_is_empty(const struct c2_tl_descr *d, const struct c2_tl *list)
{
	C2_PRE(c2_tlist_invariant(d, list));
	return c2_list_is_empty(&list->t_head);
}

bool c2_tlink_is_in(const struct c2_tl_descr *d, const void *obj)
{
	C2_PRE(c2_tlink_invariant(d, obj));
	return c2_list_link_is_in(link(d, obj));
}

bool c2_tlist_contains(const struct c2_tl_descr *d, const struct c2_tl *list,
		       const void *obj)
{
	C2_PRE(c2_tlist_invariant(d, list));
	C2_PRE(c2_tlink_invariant(d, obj));
	return c2_list_contains(&list->t_head, link(d, obj));
}

size_t c2_tlist_length(const struct c2_tl_descr *d, const struct c2_tl *list)
{
	C2_PRE(c2_tlist_invariant(d, list));
	return c2_list_length(&list->t_head);
}
C2_EXPORTED(c2_tlist_length);

void c2_tlist_add(const struct c2_tl_descr *d, struct c2_tl *list, void *obj)
{
	C2_PRE(c2_tlist_invariant(d, list));
	C2_PRE(!c2_tlink_is_in(d, obj));
	c2_list_add(&list->t_head, link(d, obj));
}

void c2_tlist_add_tail(const struct c2_tl_descr *d,
		       struct c2_tl *list, void *obj)
{
	C2_PRE(c2_tlist_invariant(d, list));
	C2_PRE(!c2_tlink_is_in(d, obj));
	c2_list_add_tail(&list->t_head, link(d, obj));
}

void c2_tlist_add_after(const struct c2_tl_descr *d, void *obj, void *new)
{
	C2_PRE(c2_tlink_is_in(d, obj));
	C2_PRE(!c2_tlink_is_in(d, new));
	c2_list_add_after(link(d, obj), link(d, new));
}

void c2_tlist_add_before(const struct c2_tl_descr *d, void *obj, void *new)
{
	C2_PRE(c2_tlink_is_in(d, obj));
	C2_PRE(!c2_tlink_is_in(d, new));
	c2_list_add_before(link(d, obj), link(d, new));
}

void c2_tlist_del(const struct c2_tl_descr *d, void *obj)
{
	C2_PRE(c2_tlink_invariant(d, obj));
	C2_PRE(c2_tlink_is_in(d, obj));
	c2_list_del(link(d, obj));
	C2_PRE(!c2_tlink_is_in(d, obj));
}

void c2_tlist_move(const struct c2_tl_descr *d, struct c2_tl *list, void *obj)
{
	C2_PRE(c2_tlist_invariant(d, list));
	C2_PRE(c2_tlink_is_in(d, obj));

	c2_list_move(&list->t_head, link(d, obj));
}

void c2_tlist_move_tail(const struct c2_tl_descr *d,
			struct c2_tl *list, void *obj)
{
	C2_PRE(c2_tlist_invariant(d, list));
	C2_PRE(c2_tlink_is_in(d, obj));

	c2_list_move_tail(&list->t_head, link(d, obj));
}

void *c2_tlist_head(const struct c2_tl_descr *d, const struct c2_tl *list)
{
	const struct c2_list *head;

	C2_PRE(c2_tlist_invariant(d, list));

	head = &list->t_head;
	return head->l_head != (void *)head ? amb(d, head->l_head) : NULL;
}

void *c2_tlist_tail(const struct c2_tl_descr *d, const struct c2_tl *list)
{
	const struct c2_list *head;

	C2_PRE(c2_tlist_invariant(d, list));

	head = &list->t_head;
	return head->l_tail != (void *)head ? amb(d, head->l_tail) : NULL;
}

void *c2_tlist_next(const struct c2_tl_descr *d,
		    const struct c2_tl *list, void *obj)
{
	struct c2_list_link *next;

	/*
	 * c2_tlist_contains() internally calls c2_tlist_invariant().
	 * c2_tl_for() uses c2_tlist_next() to adance through list elements.
	 * Hence while iterating list of N elements using c2_tl_for(), the
	 * invariant will be called N times. This resulted in severe
	 * performance issue on longer lists. Ergo commenting this pre-condition
	 *
	 * see http://reviewboard.clusterstor.com/r/852/
	 */
	/* C2_PRE(c2_tlist_contains(d, list, obj)); */

	next = link(d, obj)->ll_next;
	return (void *)next != &list->t_head ? amb(d, next) : NULL;
}

void *c2_tlist_prev(const struct c2_tl_descr *d,
		    const struct c2_tl *list, void *obj)
{
	struct c2_list_link *prev;

	C2_PRE(c2_tlist_contains(d, list, obj));

	prev = link(d, obj)->ll_prev;
	return (void *)prev != &list->t_head ? amb(d, prev) : NULL;
}

bool c2_tlist_invariant(const struct c2_tl_descr *d, const struct c2_tl *list)
{
	struct c2_list_link *head;
	struct c2_list_link *scan;

	head = (void *)&list->t_head;

	if (list->t_magic != d->td_head_magic)
		return false;
	if ((head->ll_next == head) != (head->ll_prev == head))
		return false;

	for (scan = head->ll_next; scan != head; scan = scan->ll_next) {
		if (scan->ll_next->ll_prev != scan ||
		    scan->ll_prev->ll_next != scan)
			return false;
		if (!c2_tlink_invariant(d, amb(d, scan)))
			return false;
	}
	return true;
}

bool c2_tlink_invariant(const struct c2_tl_descr *d, const void *obj)
{
	return d->td_link_magic == 0 || magic(d, obj) == d->td_link_magic;
}

static struct c2_list_link *link(const struct c2_tl_descr *d, const void *obj)
{
	return &((struct c2_tlink *)(obj + d->td_link_offset))->t_link;
}

static uint64_t magic(const struct c2_tl_descr *d, const void *obj)
{
	return *(uint64_t *)(obj + d->td_link_magic_offset);
}

static void *amb(const struct c2_tl_descr *d, struct c2_list_link *link)
{
	return (void *)container_of(link, struct c2_tlink,
				    t_link) - d->td_link_offset;
}

/** @} end of tlist group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
