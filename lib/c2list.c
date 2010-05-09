#include "c2list.h"
#include "asrt.h"

void c2_list_init(struct c2_list *head)
{
	head->first = (struct c2_list_link *)head;
	head->last = (struct c2_list_link *)head;
}

void c2_list_fini(struct c2_list *head)
{
	C2_ASSERT(c2_list_is_empty(head));
}

bool c2_list_is_empty(const struct c2_list *head)
{
	return head->first == (void *)head && head->last == (void *)head;
}
bool c2_list_invariant(const struct c2_list *head)
{
	struct c2_list_link *pos = head->first;

	while (pos != (void *)head) {
		if (pos->next->prev != pos ||
		    pos->prev->next != pos)
			return false;
		pos = pos->next;
	}

	return true;
}

size_t c2_list_length(const struct c2_list *list)
{
	size_t               length;
	struct c2_list_link *scan;

	C2_ASSERT(c2_list_invariant(list));
	length = 0;
	for (scan = list->first; scan != (void *)list; scan = scan->next)
		length++;
	return length;
}

void c2_list_link_init(struct c2_list_link *link)
{
	link->prev = link;
	link->next = link;
}

void c2_list_link_fini(struct c2_list_link *link)
{

}

bool c2_list_link_is_in(const struct c2_list_link *link)
{
	return link->prev != link;
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
