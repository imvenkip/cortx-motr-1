/* -*- C -*- */

#ifndef __COLIBRI_ADT_H__
#define __COLIBRI_ADT_H__

/**
   @defgroup adt Basic abstract data types
   @{
*/

struct c2_queue;
struct c2_stack;

struct c2_queue_link;
struct c2_stack_link;

void c2_queue_init(struct c2_queue *q);
void c2_queue_fini(struct c2_queue *q);
bool c2_queue_is_empty(const struct c2_queue *q);

void c2_stack_init(struct c2_stack *stack);
void c2_stack_fini(struct c2_stack *stack);
bool c2_stack_is_empty(const struct c2_stack *stack);

void c2_queue_link_init(struct c2_queue_link *q);
void c2_queue_link_fini(struct c2_queue_link *q);
bool c2_queue_link_is_in(const struct c2_queue_link *q);

void c2_stack_link_init(struct c2_stack_link *stack);
void c2_stack_link_fini(struct c2_stack_link *stack);
bool c2_stack_link_is_in(const struct c2_stack_link *stack);

/** extent. */
struct c2_ext {
	uint64_t e_start;
	uint64_t e_end;
};

int c2_ext_are_overlapping(const struct c2_ext *e0, const struct c2_ext *e1);
int c2_ext_is_partof(const struct c2_ext *super, const struct c2_ext *sub);
int c2_ext_is_empty(const struct c2_ext *ext);
void c2_ext_intersection(const struct c2_ext *e0, const struct c2_ext *e1,
			 struct c2_ext *result);
/* must work correctly when minuend == difference */
void c2_ext_sub(const struct c2_ext *minuend, const struct c2_ext *subtrahend,
		struct c2_ext *difference);
/* must work correctly when sum == either of terms. */
void c2_ext_add(const struct c2_ext *term0, const struct c2_ext *term1,
		struct c2_ext *sum);

/* what about signed? */
uint64_t c2_ext_cap(const struct c2_ext *ext2, uint64_t val);

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
