/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <maxim_medved@xyratex.com>
 * Original creation date: 29-May-2013
 */

#pragma once
#ifndef __MERO_BE_ALLOC_H__
#define __MERO_BE_ALLOC_H__

#include "lib/types.h"  /* m0_bcount_t */
#include "lib/mutex.h"

struct m0_be_op;
struct m0_be_seg;
struct m0_be_tx;
struct m0_be_tx_credit;

/**
 * @defgroup be
 *
 *
 * @{
 */

enum {
	/**
	 * Allocated memory will be aligned using at least this shift.
	 * @see m0_be_alloc(), m0_be_allocator_credit().
	 */
	M0_BE_ALLOC_SHIFT_MIN  = 3,
};

/**
 * @brief Allocator statistics
 *
 * It is embedded into m0_be_allocator_header.
 */
struct m0_be_allocator_stats {
	m0_bcount_t bas_free_space;
};

struct m0_be_allocator_header;

/** @brief Allocator */
struct m0_be_allocator {
	/**
	 * Memory is allocated from the segment using first-fit algorithm.
	 * Entire segment except m0_be_seg_hdr is used as a memory
	 * for allocations.
	 */
	struct m0_be_seg	      *ba_seg;
	/**
	 * Lock protects allocator lists and allocator chunks
	 * (but not allocated memory).
	 */
	struct m0_mutex		       ba_lock;
	/** Internal allocator data. It is stored inside the segment. */
	struct m0_be_allocator_header *ba_h;
};

/**
 * Initialize allocator structure.
 *
 * @see m0_be_allocator_header.
 */
M0_INTERNAL int m0_be_allocator_init(struct m0_be_allocator *a,
				     struct m0_be_seg *seg);

/**
 * Finalize allocator structure.
 *
 * It will not affect allocated memory, allocator space or allocator header.
 */
M0_INTERNAL void m0_be_allocator_fini(struct m0_be_allocator *a);

/**
 * Allocator invariant.
 *
 * It will perform detailed verefication of allocator data structures.
 * It will ignore all user data.
 */
M0_INTERNAL bool m0_be_allocator__invariant(struct m0_be_allocator *a);

/**
 * Create allocator on the segment.
 *
 * @see m0_be_allocator.ba_seg, m0_be_allocator_init(),
 * m0_be_allocator_header.
 */
M0_INTERNAL int m0_be_allocator_create(struct m0_be_allocator *a,
				       struct m0_be_tx *tx);

/**
 * Destroy allocator on the segment.
 *
 * All memory allocations obtained from m0_be_alloc()
 * should be m0_be_free()'d before calling this function.
 */
M0_INTERNAL void m0_be_allocator_destroy(struct m0_be_allocator *a,
					 struct m0_be_tx *tx);

/**
 * Allocator operation.
 *
 * @see m0_be_allocator_credit().
 */
enum m0_be_allocator_op {
	M0_BAO_CREATE,	/**< Allocator credit for m0_be_allocator_create() */
	M0_BAO_DESTROY,	/**< Allocator credit for m0_be_allocator_destroy() */
	M0_BAO_ALLOC,	/**< Allocator credit for m0_be_alloc() */
	M0_BAO_FREE,	/**< Allocator credit for m0_be_free() */
};

/**
 * Accumulate credits for optype in accum.
 *
 * @param a Allocator
 * @param optype Allocator operation type
 * @param size Size of allocation for optype = M0_BAO_ALLOC.
 *	       It is ignored for optype = M0_BAO_FREE.
 * @param shift Memory for optype = M0_BAO_ALLOC.
 *	        It is ignored for optype = M0_BAO_FREE.
 * @param accum Accumulator for credits.
 *
 * @see m0_be_alloc(), m0_be_free(), m0_be_tx_credit.
 */
M0_INTERNAL void m0_be_allocator_credit(struct m0_be_allocator *a,
					enum m0_be_allocator_op optype,
					m0_bcount_t size,
					unsigned shift,
					struct m0_be_tx_credit *accum);

/**
 * Allocate memory.
 *
 * @param a Allocator
 * @param tx Allocation will be done in this transaction
 * @param op XXX. Now m0_be_op state always set to M0_BOS_SUCCESS upon return
 *	     from this function.
 * @param size Memory size
 * @param shift Memory will be aligned on (shift^2)-byte boundary.
 *		It can be less than M0_BE_ALLOC_SHIFT_MIN - in this case
 *		allocation will be done as if it is equal to
 *		M0_BE_ALLOC_SHIFT_MIN.
 *
 * @todo use op.
 * @see m0_be_allocator_credit(), M0_BAO_ALLOC,
 * m0_alloc(), m0_alloc_aligned(), M0_BE_ALLOC_SHIFT_MIN.
 */
M0_INTERNAL void *m0_be_alloc(struct m0_be_allocator *a,
			      struct m0_be_tx *tx, struct m0_be_op *op,
			      m0_bcount_t size, unsigned shift);

/**
 * Free memory allocated with m0_be_alloc().
 *
 * @param a Allocator
 * @param tx Free operation will be done in this transaction
 * @param op XXX. Now m0_be_op state always set to M0_BOS_SUCCESS upon return
 *	     from this function.
 * @param ptr Pointer to memory to release. May be NULL - in this case
 *	      op state set to M0_BOS_SUCCESS and call to this function is
 *	      no-op. If ptr is not NULL, then it should be exactly the pointer
 *	      returned from m0_be_alloc() for allocator a. Double m0_be_free()
 *	      is not allowed for the same pointer from one call to
 *	      m0_be_alloc().
 *
 * @todo use op.
 * @see m0_be_alloc(), m0_be_allocator_credit(), M0_BAO_FREE,
 * m0_be_allocator_destroy().
 */
M0_INTERNAL void m0_be_free(struct m0_be_allocator *a,
			    struct m0_be_tx *tx, struct m0_be_op *op,
			    void *ptr);

/**
 * Return allocator statistics.
 *
 * @note Not implemented yet.
 * @see m0_be_allocator_stats.
 */
M0_INTERNAL void m0_be_alloc_stats(struct m0_be_allocator *a,
				   struct m0_be_allocator_stats *out);

/**
 * Allocate array of structures.
 *
 * It is a wrapper around m0_be_alloc().
 * @see m0_be_alloc(), M0_ALLOC_ARR().
 */
#define M0_BE_ALLOC_ARR(seg, tx, op, shift, arr, nr)			\
	((arr) = m0_be_alloc(&(seg)->bs_allocator, (tx), (op),		\
			     (nr) * sizeof ((arr)[0])), (shift))

/**
 * Allocate structure.
 *
 * It is a wrapper around m0_be_alloc().
 * @see m0_be_alloc(), M0_ALLOC_PTR(), M0_BE_ALLOC_ARR().
 */
#define M0_BE_ALLOC_PTR(seg, tx, op, shift, ptr)			\
		M0_ALLOC_ARR(seg, tx, op, shift, ptr, 1)

/** @} end of be group */
#endif /* __MERO_BE_ALLOC_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
