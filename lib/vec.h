/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 05/12/2010
 */

#ifndef __COLIBRI_LIB_VEC_H__
#define __COLIBRI_LIB_VEC_H__

#include "lib/types.h"
#include "lib/buf.h"

#ifdef __KERNEL__
#include "lib/linux_kernel/vec.h"
#endif

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
   @endcode

   invariant is maintained. This is called a "normal" state.

   @li or a cursor is in an "end of the vector" state. In this state

   @code
   cur->vc_seg == cur->vc_vec->v_nr && cur->vc_offset == 0
   @endcode

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
   Return number of bytes that the cursor have to be moved to reach the next
   segment in its vector (or to move into end of the vector position, when the
   cursor is already at the last segment).

   @pre cur->vc_seg < cur->vc_vec->v_nr
 */
c2_bcount_t c2_vec_cursor_step(const struct c2_vec_cursor *cur);

/** Vector of extents in a linear name-space */
struct c2_indexvec {
	/* Number of buffers and their sizes. */
	struct c2_vec  iv_vec;
	/** Array of starting extent indices. */
	c2_bindex_t   *iv_index;
};

/** Vector of memory buffers */
struct c2_bufvec {
	/* Number of buffers and their sizes. */
	struct c2_vec  ov_vec;
	/** Array of buffer addresses. */
	void         **ov_buf;
};

/**
   Initialize a c2_bufvec containing a single segment of the specified size.
   The intended usage is as follows:

   @code
   void *addr;
   c2_bcount_t buf_count;
   struct c2_bufvec in = C2_BUFVEC_INIT_BUF(&addr, &buf_count);

   buf_count = ...;
   addr = ...;
   @endcode
 */
#define C2_BUFVEC_INIT_BUF(addr_ptr, count_ptr)	{	\
	.ov_vec = {					\
		.v_nr = 1,				\
		.v_count = (count_ptr),			\
	},						\
	.ov_buf = (addr_ptr)				\
}

/**
   Allocates memory for a struct c2_bufvec.  All segments are of equal
   size.
   The internal struct c2_vec is also allocated by this routine.
   @pre num_segs > 0 && seg_size > 0

   @param bufvec Pointer to buffer vector to be initialized.
   @param num_segs Number of memory segments.
   @param seg_size Size of each segment.
   @retval 0 On success.
   @retval -errno On failure.
   @see c2_bufvec_free()
 */
int c2_bufvec_alloc(struct c2_bufvec *bufvec,
		    uint32_t          num_segs,
		    c2_bcount_t       seg_size);

/**
   Frees the buffers pointed to by c2_bufvec.ov_buf and
   the c2_bufvec.ov_vec vector, using c2_free().
   @param bufvec Pointer to the c2_bufvec.
   @see c2_bufvec_alloc()
 */
void c2_bufvec_free(struct c2_bufvec *bufvec);

/** Cursor to traverse a bufvec */
struct c2_bufvec_cursor {
	/** Vector cursor used to track position in the vector
	    embedded in the associated bufvec.
	 */
	struct c2_vec_cursor  bc_vc;
};

/**
   Initialize a struct c2_bufvec cursor.
   @param cur Pointer to the struct c2_bufvec_cursor.
   @param bvec Pointer to the struct c2_bufvec.
 */
void  c2_bufvec_cursor_init(struct c2_bufvec_cursor *cur,
			    struct c2_bufvec *bvec);

/**
   Advance the cursor "count" bytes further through the buffer vector.
   @see c2_vec_cursor_move()
   @param cur Pointer to the struct c2_bufvec_cursor.
   @return true, iff the end of the vector has been reached while moving. The
   cursor is in end of the vector position in this case.
   @return false otherwise
 */
bool c2_bufvec_cursor_move(struct c2_bufvec_cursor *cur, c2_bcount_t count);

/**
   Return number of bytes that the cursor have to be moved to reach the next
   segment in its vector (or to move into end of the vector position, when the
   cursor is already at the last segment).

   @pre !c2_bufvec_cursor_move(cur, 0)
   @see c2_vec_cursor_step()
   @param cur Pointer to the struct c2_bufvec_cursor.
   @retval Count
 */
c2_bcount_t c2_bufvec_cursor_step(const struct c2_bufvec_cursor *cur);

/**
   Return the buffer address at the cursor's current position.
   @pre !c2_bufvec_cursor_move(cur, 0)
   @see c2_bufvec_cursor_copy()
   @param cur Pointer to the struct c2_bufvec_cursor.
   @retval Pointer into buffer.
 */
void *c2_bufvec_cursor_addr(struct c2_bufvec_cursor *cur);

/**
   Copy bytes from one buffer to another using cursors.
   Both cursors are advanced by the number of bytes copied.
   @param dcur Pointer to the destination buffer cursor positioned
   appropriately.
   @param scur Pointer to the source buffer cursor positioned appropriately.
   @param num_bytes The number of bytes to copy.
   @retval bytes_copied The number of bytes actually copied. This will be equal
   to num_bytes only if there was adequate space in the buffers.
 */
c2_bcount_t c2_bufvec_cursor_copy(struct c2_bufvec_cursor *dcur,
				  struct c2_bufvec_cursor *scur,
				  c2_bcount_t num_bytes);

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

/**
   Zero vector is a full fledged io vector containing io extents
   as well as the io buffers. Io fop henceforth will contain
   only c2_net_buf_desc object. Hence, wherever an io fop needs to be
   processed, its zero vector needs to be populated in-memory and all
   processing happens using the zero vector.
   An invariant (zerovec_invariant) is maintained for c2_0vec. It
   always checks sanity of zero vector and keeps a bound check on
   array of io buffers using bufvec cursor.

   Zero vector is typically allocated by upper layer by following
   the bounds of network layer (max buffer size, max segments,
   max seg size) and adds buffers/pages later as and when needed.
 */
struct c2_0vec {
	/** Bufvec representing extent of io vector and array of buffers. */
	struct c2_bufvec	 z_bvec;
	/** Array of indices of target object to start IO from. */
	c2_bindex_t		*z_indices;
	/** Cursor to traverse through the zero vector. */
	struct c2_bufvec_cursor	 z_cursor;
};

/**
   Initialize a pre-allocated c2_0vec structure.
   @pre zvec != NULL.
   @param zvec The c2_0vec structure to be initialized.
   @param segs_nr Number of segments in zero vector.
   @param seg_size Size of each segment.
   @post zvec->z_bvec.ov_buf != NULL &&
   zvec->z_bvec.ov_vec.v_nr != 0 &&
   zvec->z_bvec.ov_vec.v_count != NULL &&
   zvec->z_indices != NULL
 */
int c2_0vec_init(struct c2_0vec *zvec, const uint32_t segs_nr,
		 c2_bcount_t seg_size);

/**
   Free a c2_0vec structure.
   @param The c2_0vec structure to be deallocated.
   @see c2_0vec_init.
 */
void c2_0vec_free(struct c2_0vec *zvec);

/**
   Init the c2_0vec structure from given c2_bufvec structure and
   array of indices.
   @pre zvec != NULL && bufvec != NULL && indices != NULL.
   @param zvec The c2_0vec structure to be initialized.
   @param bufvec The c2_bufvec containing buffer starting addresses and
   with number of buffers and their byte counts.
   @param indices Target object indices to start the io from.
   @post zvec->z_cursor.bc_vc.vc_seg != 0 &&
   zvec->z_cursor.bc_vc.vc_seg < zvec->z_bvec.ov_vec.v_nr
 */
int c2_0vec_bvec_add(struct c2_0vec *zvec,
		     const struct c2_bufvec *bufvec,
		     const c2_bindex_t *indices);

/**
   Init the c2_0vec structure from array of buffers with indices and counts.
   @note The c2_0vec struct should be allocated by user.

   @param zvec The c2_0vec structure to be initialized.
   @param bufs Array of io buffers.
   @param indices Array of target object indices.
   @param counts Array of buffer counts.
   @param segs_nr Number of segments contained in the buf array.
   @post zvec->z_cursor.bc_vc.vc_seg != 0 &&
   zvec->z_cursor.bc_vc.vc_seg < zvec->z_bvec.ov_vec.v_nr
 */
int c2_0vec_bufs_add(struct c2_0vec *zvec, void **bufs,
		     const c2_bindex_t *indices, const c2_bcount_t *counts,
		     uint32_t segs_nr);

/**
   Init the c2_0vec structure from a c2_buf structure and index.
   @note The c2_0vec struct should be allocated by user.

   @param zvec The c2_0vec structure to be initialized.
   @param buf The c2_buf structure containing starting address of buffer
   and number of bytes in buffer.
   @param index Index of target object to start io from.
   @post ++zvec->z_cursor.bc_vc.vc_seg;
 */
int c2_0vec_cbuf_add(struct c2_0vec *zvec, const struct c2_buf *buf,
		     const c2_bindex_t *index);

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
