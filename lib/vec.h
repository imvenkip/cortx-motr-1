/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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

#pragma once

#ifndef __MERO_LIB_VEC_H__
#define __MERO_LIB_VEC_H__

#include "lib/types.h"
#include "lib/buf.h"
#include "xcode/xcode_attr.h"

#ifdef __KERNEL__
#include "lib/linux_kernel/vec.h"
#endif

/* import */
struct m0_addb_ctx;
struct m0_addb_loc;

/**
   @defgroup vec Vectors
   @{
*/

/**
   A vector of "segments" where each segment is something having a "count".

   m0_vec is used to implement functionality common to various "scatter-gather"
   data-structures, like m0_indexvec, m0_bufvec, m0_diovec.
 */
struct m0_vec {
	/** number of segments in the vector */
	uint32_t     v_nr;
	/** array of segment counts */
	m0_bcount_t *v_count;
} M0_XCA_RECORD;

/** Returns total count of vector */
M0_INTERNAL m0_bcount_t m0_vec_count(const struct m0_vec *vec);

/**
   Position within a vector.

   m0_vec_cursor is a cursor associated with a m0_vec instance. A cursor can be
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
struct m0_vec_cursor {
	const struct m0_vec *vc_vec;
	/** Segment that the cursor is currently in. */
	uint32_t             vc_seg;
	/** Offset within the segment that the cursor is positioned at. */
	m0_bindex_t          vc_offset;
};

/**
   Initialise a cursor.

   Cursor requires no special finalisation.
 */
M0_INTERNAL void m0_vec_cursor_init(struct m0_vec_cursor *cur,
				    struct m0_vec *vec);

/**
   Move cursor count bytes further through the vector.

   m0_vec_cursor_move(cur, 0) is guaranteed to return true iff cursor is in end
   of the vector position without modifying cursor in any way.

   @return true, iff the end of the vector has been reached while moving. The
   cursor is in end of the vector position in this case.
 */
M0_INTERNAL bool m0_vec_cursor_move(struct m0_vec_cursor *cur,
				    m0_bcount_t count);

/**
   Return number of bytes that the cursor have to be moved to reach the next
   segment in its vector (or to move into end of the vector position, when the
   cursor is already at the last segment).

   @pre cur->vc_seg < cur->vc_vec->v_nr
 */
M0_INTERNAL m0_bcount_t m0_vec_cursor_step(const struct m0_vec_cursor *cur);

/** Vector of extents in a linear name-space */
struct m0_indexvec {
	/** Number of extents and their sizes. */
	struct m0_vec  iv_vec;
	/** Array of starting extent indices. */
	m0_bindex_t   *iv_index;
};

/** Vector of memory buffers */
struct m0_bufvec {
	/** Number of buffers and their sizes. */
	struct m0_vec  ov_vec;
	/** Array of buffer addresses. */
	void         **ov_buf;
};

/**
   Initialize a m0_bufvec containing a single segment of the specified size.
   The intended usage is as follows:

   @code
   void *addr;
   m0_bcount_t buf_count;
   struct m0_bufvec in = M0_BUFVEC_INIT_BUF(&addr, &buf_count);

   buf_count = ...;
   addr = ...;
   @endcode
 */
#define M0_BUFVEC_INIT_BUF(addr_ptr, count_ptr)	{	\
	.ov_vec = {					\
		.v_nr = 1,				\
		.v_count = (count_ptr),			\
	},						\
	.ov_buf = (addr_ptr)				\
}

/**
   Allocates memory for a struct m0_bufvec.  All segments are of equal
   size.
   The internal struct m0_vec is also allocated by this routine.
   @pre num_segs > 0 && seg_size > 0

   @param bufvec Pointer to buffer vector to be initialized.
   @param num_segs Number of memory segments.
   @param seg_size Size of each segment.
   @retval 0 On success.
   @retval -errno On failure.
   @see m0_bufvec_free()
 */
M0_INTERNAL int m0_bufvec_alloc(struct m0_bufvec *bufvec,
				uint32_t num_segs, m0_bcount_t seg_size);

/**
   Allocates aligned memory as specified by shift value for a struct m0_bufvec.
   Currently in kernel mode it supports PAGE_SIZE alignment only.
 */
M0_INTERNAL int m0_bufvec_alloc_aligned(struct m0_bufvec *bufvec,
					uint32_t num_segs,
					m0_bcount_t seg_size, unsigned shift);

/**
   Frees the buffers pointed to by m0_bufvec.ov_buf and
   the m0_bufvec.ov_vec vector, using m0_free().
   @param bufvec Pointer to the m0_bufvec.
   @see m0_bufvec_alloc()
 */
M0_INTERNAL void m0_bufvec_free(struct m0_bufvec *bufvec);

/**
   Frees the buffers pointed to by m0_bufvec.ov_buf and
   the m0_bufvec.ov_vec vector, using m0_free_aligned().
   @param bufvec Pointer to the m0_bufvec.
   @see m0_bufvec_alloc_aligned()
 */
M0_INTERNAL void m0_bufvec_free_aligned(struct m0_bufvec *bufvec,
					unsigned shift);

/**
 * Allocate memory for index array and counts array in index vector.
 * @param len Number of elements to allocate memory for.
 * @param ctx Addb context to log addb messages in case of failure.
 * @param loc Addb location to log addb messages in.
 * @pre   ivec != NULL && len > 0.
 * @post  ivec->iv_index != NULL && ivec->iv_vec.v_count != NULL &&
 *        ivec->iv_vec.v_nr == len.
 */
M0_INTERNAL int m0_indexvec_alloc(struct m0_indexvec *ivec, uint32_t len,
				  struct m0_addb_ctx *ctx,
				  const struct m0_addb_loc *loc);

/**
 * Deallocates the memory buffers pointed to by index array and counts array.
 * Also sets the array count to zero.
 * @pre  ivec != NULL && ivec->iv_vec.v_nr > 0.
 * @post ivec->iv_index == NULL && ivec->iv_vec.v_count == NULL &&
 *       ivec->iv_vec.v_nr == 0.
 */
M0_INTERNAL void m0_indexvec_free(struct m0_indexvec *ivec);

/** Cursor to traverse a bufvec */
struct m0_bufvec_cursor {
	/** Vector cursor used to track position in the vector
	    embedded in the associated bufvec.
	 */
	struct m0_vec_cursor  bc_vc;
};

/**
   Initialize a struct m0_bufvec cursor.
   @param cur Pointer to the struct m0_bufvec_cursor.
   @param bvec Pointer to the struct m0_bufvec.
 */
M0_INTERNAL void m0_bufvec_cursor_init(struct m0_bufvec_cursor *cur,
				       struct m0_bufvec *bvec);

/**
   Advance the cursor "count" bytes further through the buffer vector.
   @see m0_vec_cursor_move()
   @param cur Pointer to the struct m0_bufvec_cursor.
   @return true, iff the end of the vector has been reached while moving. The
   cursor is in end of the vector position in this case.
   @return false otherwise
 */
M0_INTERNAL bool m0_bufvec_cursor_move(struct m0_bufvec_cursor *cur,
				       m0_bcount_t count);

/**
   Advances the cursor with some count such that cursor will be aligned to
   "alignment".
   Return convention is same as m0_bufvec_cursor_move().
 */
M0_INTERNAL bool m0_bufvec_cursor_align(struct m0_bufvec_cursor *cur,
					uint64_t alignment);

/**
   Return number of bytes that the cursor have to be moved to reach the next
   segment in its vector (or to move into end of the vector position, when the
   cursor is already at the last segment).

   @pre !m0_bufvec_cursor_move(cur, 0)
   @see m0_vec_cursor_step()
   @param cur Pointer to the struct m0_bufvec_cursor.
   @retval Count
 */
M0_INTERNAL m0_bcount_t m0_bufvec_cursor_step(const struct m0_bufvec_cursor
					      *cur);

/**
   Return the buffer address at the cursor's current position.
   @pre !m0_bufvec_cursor_move(cur, 0)
   @see m0_bufvec_cursor_copy()
   @param cur Pointer to the struct m0_bufvec_cursor.
   @retval Pointer into buffer.
 */
M0_INTERNAL void *m0_bufvec_cursor_addr(struct m0_bufvec_cursor *cur);

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
M0_INTERNAL m0_bcount_t m0_bufvec_cursor_copy(struct m0_bufvec_cursor *dcur,
					      struct m0_bufvec_cursor *scur,
					      m0_bcount_t num_bytes);
/**
   Copy data with specified size to a cursor.
   @param dcur Pointer to the destination buffer cursor positioned
   appropriately.
   @param sdata Pointer to area where the data is to be copied from.
   @param num_bytes The number of bytes to copy.
 */
M0_INTERNAL m0_bcount_t m0_bufvec_cursor_copyto(struct m0_bufvec_cursor *dcur,
						void *sdata,
						m0_bcount_t num_bytes);
/**
   Copy data with specified size from a cursor.
   @param scur Pointer to the source buffer cursor positioned appropriately.
   @param ddata Pointer to area where the data is to be copied to.
   @num_bytes The number of bytes to copy.
 */
M0_INTERNAL m0_bcount_t m0_bufvec_cursor_copyfrom(struct m0_bufvec_cursor *scur,
						  void *ddata,
						  m0_bcount_t num_bytes);

/**
   Mechanism to traverse given index vector (m0_indexvec)
   keeping track of segment counts and vector boundary.
 */
struct m0_ivec_cursor {
        struct m0_vec_cursor ic_cur;
};

/**
   Initialize given index vector cursor.
   @param cur  Given index vector cursor.
   @param ivec Given index vector to be associated with cursor.
 */
M0_INTERNAL void m0_ivec_cursor_init(struct m0_ivec_cursor *cur,
				     struct m0_indexvec *ivec);

/**
   Moves the index vector cursor forward by @count.
   @param cur   Given index vector cursor.
   @param count Count by which cursor has to be moved.
   @ret   true  iff end of vector has been reached while
   moving cursor by @count. Returns false otherwise.
 */
M0_INTERNAL bool m0_ivec_cursor_move(struct m0_ivec_cursor *cur,
				     m0_bcount_t count);

/**
 * Moves index vector cursor forward until it reaches index @dest.
 * @pre   dest >= m0_ivec_cursor_index(cursor).
 * @param dest Index uptil which cursor has to be moved.
 * @ret   true iff end of vector has been reached while
 *             moving cursor. Returns false otherwise.
 * @post  m0_ivec_cursor_index(cursor) == to.
*/
M0_INTERNAL bool m0_ivec_cursor_move_to(struct m0_ivec_cursor *cursor,
					m0_bindex_t dest);

/**
 * Returns the number of bytes needed to move cursor to next segment in given
 * index vector.
 * @param cur Index vector to be moved.
 * @ret   Number of bytes needed to move the cursor to next segment.
 */
M0_INTERNAL m0_bcount_t m0_ivec_cursor_step(const struct m0_ivec_cursor *cur);

/**
 * Returns index at current cursor position.
 * @param cur Given index vector cursor.
 * @ret   Index at current cursor position.
 */
M0_INTERNAL m0_bindex_t m0_ivec_cursor_index(struct m0_ivec_cursor *cur);

/**
   Zero vector is a full fledged IO vector containing IO extents
   as well as the IO buffers.
   An invariant (m0_0vec_invariant) is maintained for m0_0vec. It
   always checks sanity of zero vector and keeps a bound check on
   array of IO buffers by checking buffer alignment and count check.

   Zero vector is typically allocated by upper layer by following
   the bounds of network layer (max buffer size, max segments,
   max seg size) and adds buffers/pages later as and when needed.
   Size of z_index array is same as array of buffer addresses and
   array of segment counts.
 */
struct m0_0vec {
	/** Bufvec representing extent of IO vector and array of buffers. */
	struct m0_bufvec	 z_bvec;
	/** Array of indices of target object to start IO from. */
	m0_bindex_t		*z_index;
};

enum {
	M0_0VEC_SHIFT = 12,
	M0_0VEC_ALIGN = (1 << M0_0VEC_SHIFT),
	M0_0VEC_MASK = M0_0VEC_ALIGN - 1,
	M0_SEG_SHIFT = 12,
	M0_SEG_SIZE  = 4096,
};

/**
   Initialize a pre-allocated m0_0vec structure.
   @pre zvec != NULL.
   @param zvec The m0_0vec structure to be initialized.
   @param segs_nr Number of segments in zero vector.
   @post zvec->z_bvec.ov_buf != NULL &&
   zvec->z_bvec.ov_vec.v_nr != 0 &&
   zvec->z_bvec.ov_vec.v_count != NULL &&
   zvec->z_index != NULL
 */
M0_INTERNAL int m0_0vec_init(struct m0_0vec *zvec, uint32_t segs_nr);

/**
   Finalize a m0_0vec structure.
   @param The m0_0vec structure to be deallocated.
   @see m0_0vec_init().
 */
M0_INTERNAL void m0_0vec_fini(struct m0_0vec *zvec);

/**
   Init the m0_0vec structure from given m0_bufvec structure and
   array of indices.
   This API does not copy data. Only pointers are copied.
   @pre zvec != NULL && bufvec != NULL && indices != NULL.
   @param zvec The m0_0vec structure to be initialized.
   @param bufvec The m0_bufvec containing buffer starting addresses and
   with number of buffers and their byte counts.
   @param indices Target object indices to start the IO from.
   @post m0_0vec_invariant(zvec).
 */
M0_INTERNAL void m0_0vec_bvec_init(struct m0_0vec *zvec,
				   const struct m0_bufvec *bufvec,
				   const m0_bindex_t * indices);

/**
   Init the m0_0vec structure from array of buffers with indices and counts.
   This API does not copy data. Just pointers are copied.
   @note The m0_0vec struct should be allocated by user.

   @param zvec The m0_0vec structure to be initialized.
   @param bufs Array of IO buffers.
   @param indices Array of target object indices.
   @param counts Array of buffer counts.
   @param segs_nr Number of segments contained in the buf array.
   @post m0_0vec_invariant(zvec).
 */
M0_INTERNAL void m0_0vec_bufs_init(struct m0_0vec *zvec, void **bufs,
				   const m0_bindex_t * indices,
				   const m0_bcount_t * counts,
				   uint32_t segs_nr);

/**
   Add a m0_buf structure at given target index to m0_0vec structure.
   @note The m0_0vec struct should be allocated by user.

   @param zvec The m0_0vec structure to be initialized.
   @param buf The m0_buf structure containing starting address of buffer
   and number of bytes in buffer.
   @param index Index of target object to start IO from.
   @post m0_0vec_invariant(zvec).
 */
M0_INTERNAL int m0_0vec_cbuf_add(struct m0_0vec *zvec, const struct m0_buf *buf,
				 const m0_bindex_t * index);

/**
 * Helper functions to copy opaque data with specified size to and from a
 * m0_bufvec
 */
M0_INTERNAL int m0_data_to_bufvec_copy(struct m0_bufvec_cursor *cur, void *data,
				       size_t len);

M0_INTERNAL int m0_bufvec_to_data_copy(struct m0_bufvec_cursor *cur, void *data,
				       size_t len);

/** @} end of vec group */

/* __MERO_LIB_VEC_H__ */
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
