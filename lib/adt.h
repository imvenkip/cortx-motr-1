/* -*- C -*- */

#ifndef __COLIBRI_ADT_H__
#define __COLIBRI_ADT_H__

#include <stdbool.h>
#include "cdefs.h"

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

typedef uint64_t c2_bcount_t;
typedef uint64_t c2_bindex_t;

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
uint64_t c2_ext_cap(const struct c2_ext *ext2, c2_bindex_t val);

struct c2_outseg;

struct c2_outvec {
	uint32_t          ov_nr;
	struct c2_outseg *ov_seg;
};

struct c2_outseg {
	c2_bindex_t os_index;
	c2_bcount_t os_nob;
};

c2_bcount_t c2_outvec_count   (const struct c2_outvec *vec);

struct c2_dioseg;
/**
   Array of 4KB aligned areas, suitable for direct-IO.

   @invariant size of every but last element of iovec is a multiple of 4KB.
 */
struct c2_diovec {
	uint32_t          div_nr;
	struct c2_dioseg *div_seg;
};

struct c2_dio_cookie;
struct c2_dio_engine;

struct c2_dioseg {
	void                 *die_buf;
	c2_bcount_t           die_nob;
	struct c2_dio_cookie *die_cookie;
};

enum {
	C2_DIOVEC_SHIFT = 12,
	C2_DIOVEC_ALIGN = (1 << C2_DIOVEC_SHIFT),
	C2_DIOVEC_MASK  = ~(c2_bcount_t)(C2_DIOVEC_ALIGN - 1)
};

int         c2_diovec_alloc   (struct c2_diovec *vec, 
			       void *start, c2_bcount_t nob);
void        c2_diovec_free    (struct c2_diovec *vec);
int         c2_diovec_register(struct c2_diovec *vec, 
			       struct c2_dio_engine *eng);
c2_bcount_t c2_diovec_count   (const struct c2_diovec *vec);

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
