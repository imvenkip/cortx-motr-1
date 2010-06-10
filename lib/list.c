#include "list.h"
#include "assert.h"

void c2_list_init(struct c2_list *head)
{
	C2_CASSERT(offsetof(struct c2_list, l_head) == 
		   offsetof(struct c2_list_link, ll_next));
	C2_CASSERT(offsetof(struct c2_list, l_tail) == 
		   offsetof(struct c2_list_link, ll_prev));

	head->l_head = (struct c2_list_link *)head;
	head->l_tail = (struct c2_list_link *)head;
}

void c2_list_fini(struct c2_list *head)
{
	C2_ASSERT(c2_list_is_empty(head));
}

bool c2_list_is_empty(const struct c2_list *head)
{
	return head->l_head == (void *)head && head->l_tail == (void *)head;
}
bool c2_list_invariant(const struct c2_list *head)
{
	struct c2_list_link *pos = head->l_head;

	while (pos != (void *)head) {
		if (pos->ll_next->ll_prev != pos ||
		    pos->ll_prev->ll_next != pos)
			return false;
		pos = pos->ll_next;
	}

	return true;
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

void c2_list_link_init(struct c2_list_link *link)
{
	link->ll_prev = link;
	link->ll_next = link;
}

void c2_list_link_fini(struct c2_list_link *link)
{

}

bool c2_list_link_is_in(const struct c2_list_link *link)
{
	return link->ll_prev != link;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
