/* -*- C -*- */

#ifndef __COLIBRI_LIB_LIST_H__
#define __COLIBRI_LIB_LIST_H__

#include "lib/cdefs.h"

/**
 list entry
 */
struct c2_list_link {
	/**
	 * next in link
	 */
	struct c2_list_link *prev;
	/**
	 * prev in link
	 */
	struct c2_list_link *next;
};

/**
 list head
 */
struct c2_list {
	/**
	 * pointer to first in list
	 */
	struct c2_list_link *first;
	/**
	 * pointer to last elemnt in list
	 */
	struct c2_list_link *last;
};

/**
 initialize list head
 *
 * @param head pointer to list head
 */
void c2_list_init(struct c2_list *head);
/**
 free list resources

 @param head pointer to list head
 */
void c2_list_fini(struct c2_list *head);

/**
 check list is empty

 @param head pointer to list head
 */
bool c2_list_is_empty(const struct c2_list *head);

/**
 add list to top on the list

 @param head pointer to list head
 @param new  pointer to list entry
 */
static inline void
c2_list_add(struct c2_list *head, struct c2_list_link *new)
{
	new->next = head->first;
	new->prev = (struct c2_list_link *)head;
	
	head->first = new;
	new->next->prev = new;

}

/**
 delete a entry from the list

 @param old list link entry 
 */
static inline void
c2_list_del(struct c2_list_link *old)
{
	old->prev->next = old->next;
	old->next->prev = old->prev;
}

/**
 * return first entry from the list
 *
 * @param head pointer to list head
 *
 * @return pointer to first list entry or NULL if list empty
 */
static struct c2_list_link *
c2_list_first(const struct c2_list *head)
{
	return head->first != (void *)head ? head->first : NULL ;
}

void c2_list_link_init(struct c2_list_link *head);
void c2_list_link_fini(struct c2_list_link *head);
bool c2_list_link_is_in(const struct c2_list_link *head);

/**
 * Iterate over a list
 * @param head	the head of list.
 * @param pos	the pointer to list_link to use as a loop counter.
 */
#define c2_list_for_each(head, pos) \
	for(pos = (head)->first; pos != (head)->last; \
	    pos = (pos)->next)

/**
 * get pointer to object from pointer to list link entry
 */
#define c2_list_entry(link, type, member) \
	container_of(link, type, member)

#endif
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
