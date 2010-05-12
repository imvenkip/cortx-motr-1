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
	struct c2_list_link *next;
	/**
	 * prev in link
	 */
	struct c2_list_link *prev;
};

/**
 initialize list link entry

 @param link - pointer to link enty

 @return NONE
*/
void c2_list_link_init(struct c2_list_link *link);

/**
 free resources associated with link entry

 @param link - pointer to link enty

 @return NONE
*/
void c2_list_link_fini(struct c2_list_link *link);

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
   Returns true iff @link is in @list.
 */
bool c2_list_contains(const struct c2_list *list,
		      const struct c2_list_link *link);

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
 add an entry to tail of the list.

 @param head pointer to list head
 @param new  pointer to list entry
 */
static inline void
c2_list_add_tail(struct c2_list *head, struct c2_list_link *new)
{
	new->prev = head->last;
	new->next = (struct c2_list_link *)head;
	
	head->last = new;
	new->prev->next = new;
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
 delete a entry from the list and initialize it

 @param old list link entry 
 */
static inline void
c2_list_del_init(struct c2_list_link *old)
{
	old->prev->next = old->next;
	old->next->prev = old->prev;
	c2_list_link_init(old);
}


/**
 * return first entry from the list
 *
 * @param head pointer to list head
 *
 * @return pointer to first list entry or NULL if list empty
 */
static inline struct c2_list_link *
c2_list_first(const struct c2_list *head)
{
	return head->first != (void *)head ? head->first : NULL ;
}


/**
 is link entry connected to the list
 
 @param link - pointer to link entry
 
 @retval true - entry connected to a list
 @retval false - entry disconnected from a list
*/
bool c2_list_link_is_in(const struct c2_list_link *link);

/**
 * get pointer to object from pointer to list link entry
 */
#define c2_list_entry(link, type, member) \
	container_of(link, type, member)

/**
 * Iterate over a list
 * @param head	the head of list.
 * @param pos	the pointer to list_link to use as a loop counter.
 */
#define c2_list_for_each(head, pos) \
	for(pos = (head)->first; pos != (void *)(head); \
	    pos = (pos)->next)

/**
 Iterate over a list
 
 @param head	the head of list.
 @param pos	the pointer to list_link to use as a loop counter.
 */
#define c2_list_for_each_entry(head, pos, type, member) \
	for(pos = c2_list_entry((head)->first, type, member); \
	    &(pos->member) != (void *)head; \
	    pos = c2_list_entry((pos)->member.next, type, member))


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
