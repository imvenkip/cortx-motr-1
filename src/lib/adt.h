/* -*- C -*- */

#ifndef __LIB_ADT_H__
#define __LIB_ADT_H__

/**
   @defgroup adt Basic abstract data types
   @{
*/

struct c2_queue;
struct c2_stack;
struct c2_list;

struct c2_queue_link;
struct c2_stack_link;
struct c2_list_link;

void c2_queue_init(struct c2_queue *q);
void c2_queue_fini(struct c2_queue *q);
bool c2_queue_is_empty(struct c2_queue *q);

void c2_stack_init(struct c2_stack *stack);
void c2_stack_fini(struct c2_stack *stack);
bool c2_stack_is_empty(struct c2_stack *stack);

void c2_list_init(struct c2_list *head);
void c2_list_fini(struct c2_list *head);
bool c2_list_is_empty(struct c2_list *head);

void c2_queue_link_init(struct c2_queue_link *q);
void c2_queue_link_fini(struct c2_queue_link *q);
bool c2_queue_link_is_in(struct c2_queue_link *q);

void c2_stack_link_init(struct c2_stack_link *stack);
void c2_stack_link_fini(struct c2_stack_link *stack);
bool c2_stack_link_is_in(struct c2_stack_link *stack);

void c2_list_link_init(struct c2_list_link *head);
void c2_list_link_fini(struct c2_list_link *head);
bool c2_list_link_is_in(struct c2_list_link *head);

/** @} end of adt group */


/* __LIB_ADT_H__ */
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
