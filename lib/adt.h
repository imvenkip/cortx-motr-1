/* -*- C -*- */

#ifndef __COLIBRI_ADT_H__
#define __COLIBRI_ADT_H__

#include "lib/types.h"
#include "lib/cdefs.h"

/**
   @defgroup adt Basic abstract data types
   @{
*/

struct c2_stack;

struct c2_stack_link;

void c2_stack_init(struct c2_stack *stack);
void c2_stack_fini(struct c2_stack *stack);
bool c2_stack_is_empty(const struct c2_stack *stack);

void c2_stack_link_init(struct c2_stack_link *stack);
void c2_stack_link_fini(struct c2_stack_link *stack);
bool c2_stack_link_is_in(const struct c2_stack_link *stack);

/** @} end of adt group */


/* __COLIBRI_ADT_H__ */
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
