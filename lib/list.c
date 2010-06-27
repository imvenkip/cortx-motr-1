#include "list.h"
#include "assert.h"

/** @addtogroup list @{ */

void c2_list_init(struct c2_list *head)
{
	head->l_head = (struct c2_list_link *)head;
	head->l_tail = (struct c2_list_link *)head;
}

void c2_list_fini(struct c2_list *head)
{
	C2_ASSERT(c2_list_is_empty(head));
}

bool c2_list_is_empty(const struct c2_list *head)
{
	return head->l_head == (void *)head;
}

bool c2_list_link_invariant(const struct c2_list_link *link)
{
	struct c2_list_link *scan;

	if ((link->ll_next == link) != (link->ll_prev == link))
		return false;

	for (scan = link->ll_next; scan != link; scan = scan->ll_next) {
		if (scan->ll_next->ll_prev != scan ||
		    scan->ll_prev->ll_next != scan)
			return false;
	}
	return true;
}

bool c2_list_invariant(const struct c2_list *head)
{
	return c2_list_link_invariant((void *)head);
}

size_t c2_list_length(const struct c2_list *list)
{
	size_t               length;
	struct c2_list_link *scan;

	C2_ASSERT(c2_list_invariant(list));
	length = 0;
	for (scan = list->l_head; scan != (void *)list; scan = scan->ll_next)
		length++;
	return length;
}

static inline void __c2_list_add(struct c2_list_link *next,
				 struct c2_list_link *prev,
			         struct c2_list_link *new)
{
	C2_ASSERT(prev->ll_next == next && next->ll_prev == prev);
	C2_ASSERT(c2_list_link_invariant(next));
	new->ll_next = next;
	new->ll_prev = prev;
	
	next->ll_prev = new;
	prev->ll_next = new;
	C2_ASSERT(c2_list_link_invariant(next));
}

void c2_list_add(struct c2_list *head, struct c2_list_link *new)
{
	__c2_list_add(head->l_head, (void *)head, new);
}

void c2_list_add_tail(struct c2_list *head, struct c2_list_link *new)
{
	__c2_list_add((void *)head, head->l_tail, new);
}

static void __c2_list_del(struct c2_list_link *old)
{
	C2_ASSERT(c2_list_link_invariant(old));
	old->ll_prev->ll_next = old->ll_next;
	old->ll_next->ll_prev = old->ll_prev;
}

void c2_list_del(struct c2_list_link *old)
{
	__c2_list_del(old);
	c2_list_link_init(old);
}

void c2_list_move(struct c2_list *head, struct c2_list_link *old)
{
	__c2_list_del(old);
	c2_list_add(head, old);
	C2_ASSERT(c2_list_invariant(head));
}

void c2_list_move_tail(struct c2_list *head, struct c2_list_link *old)
{
	__c2_list_del(old);
	c2_list_add_tail(head, old);
	C2_ASSERT(c2_list_invariant(head));
}

void c2_list_link_init(struct c2_list_link *link)
{
	link->ll_prev = link;
	link->ll_next = link;
}

void c2_list_link_fini(struct c2_list_link *link)
{
	C2_ASSERT(!c2_list_link_is_in(link));
}

bool c2_list_link_is_in(const struct c2_list_link *link)
{
	return link->ll_prev != link;
}

/** @} end of list group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
