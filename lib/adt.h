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

/**
   A vector of "segments" where each segment is something having a "count".

   c2_vec is used to implement functionality common to various "scatter-gather"
   data-structures, like c2_indexvec, c2_bufvec, c2_diovec.
 */
struct c2_vec {
	/** number of segments in the vector */
	uint32_t     v_nr;
	/** array of segment counts */
	c2_bcount_t *v_count;
};

/** Returns total count of vector */
c2_bcount_t c2_vec_count(const struct c2_vec *vec);

/**
   Position without a vector
 */
struct c2_vec_cursor {
	const struct c2_vec *vc_vec;
	/** Segment that the cursor is currently in. */
	uint32_t             vc_seg;
	/** Offset within the segment that the cursor is positioned at. */
	c2_bindex_t          vc_offset;
};

void c2_vec_cursor_init(struct c2_vec_cursor *cur, struct c2_vec *vec);
/**
   Move cursor count bytes further through the vector.

   @return true, iff the end of the vector has been reached while moving. The
   cursor remains at the last position in the vector in this case.
 */
bool c2_vec_cursor_move(struct c2_vec_cursor *cur, c2_bcount_t count);
/**
   Return number of bytes that are left in the segment the cursor is currently
   at.
 */
c2_bcount_t c2_vec_cursor_step(const struct c2_vec_cursor *cur);

/** Vector of extents in a linear name-space */
struct c2_indexvec {
	/* Number of buffers and their sizes. */
	struct c2_vec  ov_vec;
	/** Array of starting extent indices. */
	c2_bindex_t   *ov_index;
};

/** Vector of memory buffers */
struct c2_bufvec {
	/* Number of buffers and their sizes. */
	struct c2_vec  ov_vec;
	/** Array of buffer addresses. */
	void         **ov_buf;
};

struct c2_dio_cookie;
struct c2_dio_engine;

/**
   Array of 4KB aligned areas, suitable for direct-IO.

   @invariant size of every but last element of iovec is a multiple of 4KB.
 */
struct c2_diovec {
	struct c2_bufvec      div_vec;
	struct c2_dio_cookie *div_seg;
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
