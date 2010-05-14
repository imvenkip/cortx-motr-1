#include "c2list.h"
#include "lib/assert.h"

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


