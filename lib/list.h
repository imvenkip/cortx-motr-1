/* -*- C -*- */

#ifndef __COLIBRI_LIB_LIST_H__
#define __COLIBRI_LIB_LIST_H__

#include <sys/types.h>

#include "cdefs.h"

/**
   @defgroup list

   Double linked list.
 */

/**
   List entry.
 */
struct c2_list_link {
	/**
	 * Next entry in the list
	 */
	struct c2_list_link *ll_next;
	/**
	 * Previous entry in the list
	 */
	struct c2_list_link *ll_prev;
};

/**
 initialize list link entry

 @param link - pointer to link enty
*/
void c2_list_link_init(struct c2_list_link *link);

/**
 free resources associated with link entry

 @param link - pointer to link enty
*/
void c2_list_link_fini(struct c2_list_link *link);

/**
   List head.

   @note it is necessary that this structure has exactly the same layout as
   c2_list_link. It is checked by assertions in c2_list_init().
 */
struct c2_list {
	/**
	 * Pointer to the first entry in the list.
	 */
	struct c2_list_link *l_head;
	/**
	 * Pointer to the last entry in the list.
	 */
	struct c2_list_link *l_tail;
};

/**
   Initializes list head.
 */
void c2_list_init(struct c2_list *head);

/**
   Finalizes the list.
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
 This function iterate over the argument list checking that double-linked
 list invariant holds (x->ll_prev->ll_next == x && x->ll_next->ll_prev == x).

 @return true iff @list isn't corrupted
*/
bool c2_list_invariant(const struct c2_list *list);

size_t c2_list_length(const struct c2_list *list);

static inline void __c2_list_add(struct c2_list_link *next,
				 struct c2_list_link *prev,
			         struct c2_list_link *new)
{
	new->ll_next = next;
	new->ll_prev = prev;
	
	next->ll_prev = new;
	prev->ll_next = new;
}

/**
 add list to top on the list

 @param head pointer to list head
 @param new  pointer to list entry
 */
static inline void c2_list_add(struct c2_list *head, struct c2_list_link *new)
{
	__c2_list_add(head->l_head, (void *)head, new);
}

/**
 add list to tail on the list

 @param head pointer to list head
 @param new  pointer to list entry
 */
static inline void c2_list_add_tail(struct c2_list *head, struct c2_list_link *new)
{
	__c2_list_add((void *)head, head->l_tail, new);
}

/**
 delete a entry from the list

 @param old list link entry
 */
static inline void c2_list_del(struct c2_list_link *old)
{
	old->ll_prev->ll_next = old->ll_next;
	old->ll_next->ll_prev = old->ll_prev;
}

/**
 delete a entry from the list and initialize it

 @param old list link entry
 */
static inline void c2_list_del_init(struct c2_list_link *old)
{
	old->ll_prev->ll_next = old->ll_next;
	old->ll_next->ll_prev = old->ll_prev;
	c2_list_link_init(old);
}


/**
 * return first entry from the list
 *
 * @param head pointer to list head
 *
 * @return pointer to first list entry or NULL if list empty
 */
static inline struct c2_list_link *c2_list_first(const struct c2_list *head)
{
	return head->l_head != (void *)head ? head->l_head : NULL ;
}


/**
 is link entry connected to the list

 @param link - pointer to link entry

 @retval true - entry connected to a list
 @retval false - entry disconnected from a list
*/
bool c2_list_link_is_in(const struct c2_list_link *link);

size_t c2_list_length(const struct c2_list *list);

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
	for(pos = (head)->l_head; pos != (void *)(head); \
	    pos = (pos)->ll_next)

/**
 Iterate over a list

 @param head	the head of list.
 @param pos	the pointer to list_link to use as a loop counter.
 */
#define c2_list_for_each_entry(head, pos, type, member) \
	for(pos = c2_list_entry((head)->l_head, type, member); \
	    &(pos->member) != (void *)head; \
	    pos = c2_list_entry((pos)->member.ll_next, type, member))


/** @} end of list group */

/* __COLIBRI_LIB_LIST_H__ */
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
