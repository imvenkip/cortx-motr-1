/* -*- C -*- */

#ifndef __COLIBRI_LIB_VEC_H__
#define __COLIBRI_LIB_VEC_H__

#include "lib/types.h"
#include "lib/adt.h"

/**
   @defgroup vec Vectors
   @{
*/

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
   Position within a vector.

   c2_vec_cursor is a cursor associated with a c2_vec instance. A cursor can be
   moved in the forward direction.

   A cursor can be in one of the two exclusive states:

   @li it is positioned within one of the vector segments. In this state

   @code
   cur->vc_seg < cur->vc_vec->v_nr &&
   cur->vc_offset < cur->vc_vec->v_count[cur->vc_seg]
   @code

   invariant is maintained. This is called a "normal" state.

   @li or a cursor is in an "end of the vector" state. In this state

   @code
   cur->vc_seg == cur->vc_vec->v_nr && cur->vc_offset == 0
   @code

   Note that a cursor over an empty vector (one with vec::v_nr == 0) is always
   in the end of the vector state.

   Also note, that according to the normal state invariant, a cursor cannot be
   positioned in an empty segment (one with zero count). Empty segments are
   skipped over by all cursor manipulating functions, including constructor.
 */
struct c2_vec_cursor {
	const struct c2_vec *vc_vec;
	/** Segment that the cursor is currently in. */
	uint32_t             vc_seg;
	/** Offset within the segment that the cursor is positioned at. */
	c2_bindex_t          vc_offset;
};

/**
   Initialise a cursor.

   Cursor requires no special finalisation.
 */
void c2_vec_cursor_init(struct c2_vec_cursor *cur, struct c2_vec *vec);

/**
   Move cursor count bytes further through the vector.

   c2_vec_cursor_move(cur, 0) is guaranteed to return true iff cursor is in end
   of the vector position without modifying cursor in any way.

   @return true, iff the end of the vector has been reached while moving. The
   cursor is in end of the vector position in this case.
 */
bool c2_vec_cursor_move(struct c2_vec_cursor *cur, c2_bcount_t count);

/**
   Return number of bytes that the cursor have to be moved to reach next segment
   in its vector (or to move into end of the vector position, when the cursor is
   already at the last segment).

   @pre cur->vc_seg < cur->vc_vec->v_nr
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

/**
   Frees all bufvec buffers.
 */
void c2_bufvec_free(struct c2_bufvec *bufvec);

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
/** @} end of vec group */

/* __COLIBRI_LIB_VEC_H__ */
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
