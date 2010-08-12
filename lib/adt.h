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

/** count of bytes (in extent, IO operation, etc.) */
typedef uint64_t c2_bcount_t;
/** an index (offset) in a linear name-space (e.g., in a file, storage object,
    storage device, memory buffer) measured in bytes */
typedef uint64_t c2_bindex_t;

enum {
	C2_BCOUNT_MAX = 0xffffffffffffffff,
	C2_BINDEX_MAX = C2_BCOUNT_MAX - 1
};

/** extent. */
struct c2_ext {
	c2_bindex_t e_start;
	c2_bindex_t e_end;
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
c2_bindex_t c2_ext_cap(const struct c2_ext *ext2, c2_bindex_t val);

struct c2_buf {
	void       *b_addr;
	c2_bcount_t b_nob;
};

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
