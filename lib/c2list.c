#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "c2list.h"

void c2_list_init(struct c2_list *head)
{
	head->first = (struct c2_list_link *)head;
	head->last = (struct c2_list_link *)head;
}

void c2_list_fini(struct c2_list *head)
{

}

bool c2_list_is_empty(const struct c2_list *head)
{
	return head->first == head->last;
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

