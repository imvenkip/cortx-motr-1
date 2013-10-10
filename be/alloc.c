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

#include "be/alloc.h"
#include "be/alloc_internal.h"
#include "be/seg_internal.h"    /* m0_be_seg_hdr */
#include "be/tx.h"              /* M0_BE_TX_CAPTURE_PTR */
#include "be/be.h"              /* m0_be_op */
#include "lib/memory.h"         /* m0_addr_is_aligned */
#include "lib/errno.h"          /* ENOSPC */
#include "lib/misc.h"		/* memset */
#include "mero/magic.h"

/**
 * @addtogroup be
 * @todo make a doxygen page
 *
 * Overview
 *
 * Definitions
 *
 * - allocator segment - memory segment (m0_be_seg) which is used as
 *   memory for allocations;
 * - allocator space - part of memory inside the allocator segment in which
 *   all allocations will take place;
 * - "B located just after A" - there is no free space in memory
 *   after A before B, i.e. (char *) B == (char *) A + sizeof(A).
 * - chunk - memory structure that contains allocator data and user data;
 * - used chunk - chunk for which address of user data was returned
 *   to user from m0_be_alloc() and for which m0_be_free() wasn't called;
 * - free chunk - chunk that is not used;
 * - adjacent chunks - a and b are adjacent chunks iff chunk a
 *   located just after b or vice versa;
 *
 * Algorithm
 *
 * m0_be_alloc(): first-fit algorithm is used.
 * Time complexity O(N), I/O complexity O(N).
 *
 * List of free chunks is scanned until first chunk that fit the alloation
 * requirements (chunk can be split somehow to obtain the chunk with
 * size >= requested size and for this chunk start of user data should be
 * alighed according to "shift" parameter) found. Then free chunk is split
 * into one or more chunks, and at least one of them should meet the allocation
 * requirements - pointer to user data of this chunk will be returned as a
 * result of m0_be_alloc().
 *
 * m0_be_free().
 * Time complexity O(N), I/O complexity O(N).
 *
 * Chunk for the given pointer is marked as free and it is added to the free
 * list. Then this chunk is merged with adjacent free chunks if any exists.
 *
 * Allocator space restrictions:
 * - Each byte of allocator space belongs to a chunk. There is one exception -
 *   if there is no space for chunk with at least 1 byte of user data from
 *   the beginning of allocator space to other chunk then this space is
 *   temporary unused;
 *
 * Chunk restrictions:
 * - all chunks are mutually disjoint;
 * - chunk is entirely inside the allocator space;
 * - each chunk is either free or used;
 * - user data in chunk located just after allocator data;
 *
 * Two lists of chunks are maintained:
 * - m0_be_allocator_header.bah_chunks contains all chunks - the chunks list;
 * - m0_be_allocator_header.bah_free contains free chunks - the free list.
 *
 * Lists of chunks restrictions:
 * - all chunks in a list are ordered by address;
 * - every chunk is in the chunks list;
 * - every free chunk is in the free list;
 * - free list don't contains used chunk;
 * - any two unequal to each other chunks in the free list are not adjacent;
 *
 * Special cases
 *
 * Chunk split in be_alloc_chunk_split()
 *
 * @verbatim
 * |		   |	|	   | |	      |	|	 |
 * +---------------+	+----------+ +--------+ |	 |
 * |	prev	   |	|  prev	   | |	      | |	 |
 * +---------------+	+----------+ |  prev  | |	 | < start0
 * |		   |	|  chunk0  | |	      | |	 |
 * |		   |	+----------+ +--------+ +--------+ < start_new
 * |    c	   |	|	   | |	      | |	 |
 * |		   |	|  new	   | |  new   | |  new	 |
 * |		   |	|	   | |	      | |	 |
 * |		   |	+----------+ +--------+ |	 | < start1
 * |		   |	|  chunk1  | |	      | |	 |
 * +---------------+	+----------+ |	      | +--------+ < start_next
 * |	next	   |	|  next	   | |	      | |  next	 |
 * +---------------+	+----------+ |	      | +--------+
 * |		   |	|	   | |	      |	|	 |
 *
 * initial position	after split  no space	 no space
 *				     for chunk0  for chunk1
 * @endverbatim
 *
 * Free chunks merge if it is possible in be_alloc_chunk_trymerge()
 *
 * @verbatim
 * |	      | |	   |
 * +----------+ +----------+
 * |	      |	|	   |
 * |	a     |	|	   |
 * |	      |	|	   |
 * |	      |	|	   |
 * +----------+	|	   |
 * |	      |	|    a	   |
 * |	      |	|	   |
 * |	      |	|	   |
 * |	b     |	|	   |
 * |	      |	|	   |
 * |	      |	|	   |
 * |	      |	|	   |
 * +----------+	+----------+
 * |	      | |	   |
 * @endverbatim
 *
 * Locks
 *
 * Allocator lock is used to protect all allocator data. Only one allocation or
 * freeing may take place at a some point of time for the same allocator.
 *
 * Implementation notes:
 * - If tx parameter is NULL then allocator will not capture segment updates.
 *   It might be useful in the allocator UT.
 *
 * Know issues:
 * - op is unconditionally transitioned to state M0_BOS_SUCCESS in m0_be_alloc()
 *   and m0_be_free().
 */

/*
 * @addtogroup be
 *
 * @{
 */

enum {
	/** alignment for m0_be_allocator_header inside segment */
	BE_ALLOC_HEADER_SHIFT = 3,
};

M0_TL_DESCR_DEFINE(chunks_all, "list of all chunks in m0_be_allocator",
		   static, struct be_alloc_chunk, bac_linkage, bac_magic,
		   M0_BE_ALLOC_ALL_LINK_MAGIC, M0_BE_ALLOC_ALL_MAGIC);
M0_TL_DEFINE(chunks_all, static, struct be_alloc_chunk);

M0_TL_DESCR_DEFINE(chunks_free, "list of free chunks in m0_be_allocator",
		   static, struct be_alloc_chunk,
		   bac_linkage_free, bac_magic_free,
		   M0_BE_ALLOC_FREE_LINK_MAGIC, M0_BE_ALLOC_FREE_MAGIC);
M0_TL_DEFINE(chunks_free, static, struct be_alloc_chunk);

static void be_alloc_chunk_capture(struct m0_be_allocator *a,
				   struct m0_be_tx *tx,
				   struct be_alloc_chunk *c)
{
	if (tx == NULL)
		return;
	if (c == NULL)
		return;
	M0_BE_TX_CAPTURE_PTR(a->ba_seg, tx, c);
}

static void be_alloc_head_capture(struct m0_be_allocator *a,
				  struct m0_be_tx *tx)
{
	if (tx == NULL)
		return;
	M0_BE_TX_CAPTURE_PTR(a->ba_seg, tx, a->ba_h);
}

static void chunks_all_tlist_capture_around(struct m0_be_allocator *a,
					    struct m0_be_tx *tx,
					    struct be_alloc_chunk *c)
{
	struct be_alloc_chunk *cprev;
	struct be_alloc_chunk *cnext;

	cprev = chunks_all_tlist_prev(&a->ba_h->bah_chunks.bl_list, c);
	cnext = chunks_all_tlist_next(&a->ba_h->bah_chunks.bl_list, c);
	be_alloc_chunk_capture(a, tx, c);
	be_alloc_chunk_capture(a, tx, cprev);
	be_alloc_chunk_capture(a, tx, cnext);
	be_alloc_head_capture(a, tx);
}

static void chunks_free_tlist_capture_around(struct m0_be_allocator *a,
					     struct m0_be_tx *tx,
					     struct be_alloc_chunk *c)
{
	struct be_alloc_chunk *fprev;
	struct be_alloc_chunk *fnext;

	fprev = chunks_free_tlist_prev(&a->ba_h->bah_free.bl_list, c);
	fnext = chunks_free_tlist_next(&a->ba_h->bah_free.bl_list, c);
	be_alloc_chunk_capture(a, tx, c);
	be_alloc_chunk_capture(a, tx, fprev);
	be_alloc_chunk_capture(a, tx, fnext);
	be_alloc_head_capture(a, tx);
}

/** @todo XXX temporary wrappers for a list functions */
static void chunks_all_tlink_init_c(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct be_alloc_chunk *c)
{
	chunks_all_tlink_init(c);
	be_alloc_chunk_capture(a, tx, c);
}

static void chunks_free_tlink_init_c(struct m0_be_allocator *a,
				     struct m0_be_tx *tx,
				     struct be_alloc_chunk *c)
{
	chunks_free_tlink_init(c);
	be_alloc_chunk_capture(a, tx, c);
}

static void chunks_all_tlink_fini_c(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct be_alloc_chunk *c)
{
	chunks_all_tlink_fini(c);
	be_alloc_chunk_capture(a, tx, c);
}

static void chunks_free_tlink_fini_c(struct m0_be_allocator *a,
				     struct m0_be_tx *tx,
				     struct be_alloc_chunk *c)
{
	chunks_free_tlink_fini(c);
	be_alloc_chunk_capture(a, tx, c);
}

static void chunks_all_tlist_del_c(struct m0_be_allocator *a,
				   struct m0_be_tx *tx,
				   struct be_alloc_chunk *c)
{
	struct be_alloc_chunk *cprev;
	struct be_alloc_chunk *cnext;

	cprev = chunks_all_tlist_prev(&a->ba_h->bah_chunks.bl_list, c);
	cnext = chunks_all_tlist_next(&a->ba_h->bah_chunks.bl_list, c);
	chunks_all_tlist_del(c);
	be_alloc_chunk_capture(a, tx, c);
	be_alloc_chunk_capture(a, tx, cprev);
	be_alloc_chunk_capture(a, tx, cnext);
	be_alloc_head_capture(a, tx);
}

static void chunks_free_tlist_del_c(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct be_alloc_chunk *c)
{
	struct be_alloc_chunk *fprev;
	struct be_alloc_chunk *fnext;

	fprev = chunks_free_tlist_prev(&a->ba_h->bah_free.bl_list, c);
	fnext = chunks_free_tlist_next(&a->ba_h->bah_free.bl_list, c);
	chunks_free_tlist_del(c);
	be_alloc_chunk_capture(a, tx, c);
	be_alloc_chunk_capture(a, tx, fprev);
	be_alloc_chunk_capture(a, tx, fnext);
	be_alloc_head_capture(a, tx);
}

static void chunks_free_tlist_add_tail_c(struct m0_be_allocator *a,
					 struct m0_be_tx *tx,
					 struct be_alloc_chunk *c)
{
	chunks_free_tlist_add_tail(&a->ba_h->bah_free.bl_list, c);
	chunks_free_tlist_capture_around(a, tx, c);
}

static void chunks_free_tlist_add_before_c(struct m0_be_allocator *a,
					   struct m0_be_tx *tx,
					   struct be_alloc_chunk *next,
					   struct be_alloc_chunk *c)
{
	chunks_free_tlist_add_before(next, c);
	chunks_free_tlist_capture_around(a, tx, c);
	chunks_free_tlist_capture_around(a, tx, next);
}

static void chunks_all_tlist_add_after_c(struct m0_be_allocator *a,
					 struct m0_be_tx *tx,
					 struct be_alloc_chunk *c,
					 struct be_alloc_chunk *new)
{
	chunks_all_tlist_add_after(c, new);
	chunks_all_tlist_capture_around(a, tx, c);
	chunks_all_tlist_capture_around(a, tx, new);
}

static void chunks_free_tlist_add_after_c(struct m0_be_allocator *a,
					  struct m0_be_tx *tx,
					  struct be_alloc_chunk *c,
					  struct be_alloc_chunk *new)
{
	chunks_free_tlist_add_after(c, new);
	chunks_free_tlist_capture_around(a, tx, c);
	chunks_free_tlist_capture_around(a, tx, new);
}

static void chunks_all_tlist_add_c(struct m0_be_allocator *a,
				   struct m0_be_tx *tx,
				   struct be_alloc_chunk *new)
{
	chunks_all_tlist_add(&a->ba_h->bah_chunks.bl_list, new);
	chunks_all_tlist_capture_around(a, tx, new);
}

static void chunks_free_tlist_add_c(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct be_alloc_chunk *new)
{
	chunks_free_tlist_add(&a->ba_h->bah_free.bl_list, new);
	chunks_free_tlist_capture_around(a, tx, new);
}

static void chunks_all_tlist_init_c(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct m0_tl *l)
{
	chunks_all_tlist_init(l);
	be_alloc_head_capture(a, tx);
}

static void chunks_free_tlist_init_c(struct m0_be_allocator *a,
				     struct m0_be_tx *tx,
				     struct m0_tl *l)
{
	chunks_free_tlist_init(l);
	be_alloc_head_capture(a, tx);
}

static void chunks_all_tlist_fini_c(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct m0_tl *l)
{
	chunks_all_tlist_fini(l);
	be_alloc_head_capture(a, tx);
}

static void chunks_free_tlist_fini_c(struct m0_be_allocator *a,
				     struct m0_be_tx *tx,
				     struct m0_tl *l)
{
	chunks_free_tlist_fini(l);
	be_alloc_head_capture(a, tx);
}

static bool be_alloc_is_mem_in_allocator(struct m0_be_allocator *a,
					 m0_bcount_t size, const void *ptr)
{
	return ptr >= a->ba_h->bah_addr &&
	       ptr + size <= a->ba_h->bah_addr + a->ba_h->bah_size;
}

static bool be_alloc_is_chunk_in_allocator(struct m0_be_allocator *a,
					   const struct be_alloc_chunk *c)
{
	return be_alloc_is_mem_in_allocator(a, sizeof *c + c->bac_size, c);
}

static bool be_alloc_chunk_is_not_overlapping(const struct be_alloc_chunk *a,
					      const struct be_alloc_chunk *b)
{
	return a == NULL || b == NULL ||
	       (a < b && &a->bac_mem[a->bac_size] <= (char *) b);
}

static bool be_alloc_chunk_invariant(struct m0_be_allocator *a,
				     const struct be_alloc_chunk *c)
{
	struct be_alloc_chunk *cprev;
	struct be_alloc_chunk *cnext;
	struct be_alloc_chunk *fprev;
	struct be_alloc_chunk *fnext;

	cprev = chunks_all_tlist_prev(&a->ba_h->bah_chunks.bl_list, c);
	cnext = chunks_all_tlist_next(&a->ba_h->bah_chunks.bl_list, c);
	if (c->bac_free) {
		fprev = chunks_free_tlist_prev(&a->ba_h->bah_free.bl_list, c);
		fnext = chunks_free_tlist_next(&a->ba_h->bah_free.bl_list, c);
	} else {
		fprev = NULL;
		fnext = NULL;
	}

	return _0C(c != NULL) &&
	       _0C(be_alloc_is_chunk_in_allocator(a, c)) &&
	       _0C(ergo(cnext != NULL,
			be_alloc_is_chunk_in_allocator(a, cnext))) &&
	       _0C(ergo(cprev != NULL,
			be_alloc_is_chunk_in_allocator(a, cprev))) &&
	       _0C(ergo(fnext != NULL,
			be_alloc_is_chunk_in_allocator(a, fnext))) &&
	       _0C(ergo(fprev != NULL,
			be_alloc_is_chunk_in_allocator(a, fprev))) &&
	       _0C(c->bac_magic0 == M0_BE_ALLOC_MAGIC0) &&
	       _0C(c->bac_magic1 == M0_BE_ALLOC_MAGIC1) &&
	       _0C(be_alloc_chunk_is_not_overlapping(cprev, c)) &&
	       _0C(be_alloc_chunk_is_not_overlapping(c, cnext)) &&
	       _0C(be_alloc_chunk_is_not_overlapping(fprev, c)) &&
	       _0C(be_alloc_chunk_is_not_overlapping(c, fnext)) &&
	       _0C(ergo(fprev != cprev,
		    be_alloc_chunk_is_not_overlapping(fprev, cprev))) &&
	       _0C(ergo(cnext != fnext,
		    be_alloc_chunk_is_not_overlapping(cnext, fnext)));
}

static void be_alloc_chunk_init(struct m0_be_allocator *a,
				struct m0_be_tx *tx,
				struct be_alloc_chunk *c,
				m0_bcount_t size, bool free)
{
	*c = (struct be_alloc_chunk) {
		.bac_magic0 = M0_BE_ALLOC_MAGIC0,
		.bac_size   = size,
		.bac_free   = free,
		.bac_magic1 = M0_BE_ALLOC_MAGIC1,
	};
	chunks_all_tlink_init_c(a, tx, c);
	chunks_free_tlink_init_c(a, tx, c);
}

static void be_alloc_chunk_del_fini(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct be_alloc_chunk *c)
{
	M0_PRE(be_alloc_chunk_invariant(a, c));
	if (c->bac_free)
		chunks_free_tlist_del_c(a, tx, c);
	chunks_all_tlist_del_c(a, tx, c);
	chunks_free_tlink_fini_c(a, tx, c);
	chunks_all_tlink_fini_c(a, tx, c);
}

static struct be_alloc_chunk *be_alloc_chunk_addr(void *ptr)
{
	return container_of(ptr, struct be_alloc_chunk, bac_mem);
}

static struct be_alloc_chunk *be_alloc_chunk_prev(struct m0_be_allocator *a,
						  struct be_alloc_chunk *c)
{
	struct be_alloc_chunk *r;

	r = chunks_all_tlist_prev(&a->ba_h->bah_chunks.bl_list, c);
	M0_ASSERT(ergo(r != NULL, be_alloc_chunk_invariant(a, r)));
	return r;
}

static struct be_alloc_chunk *be_alloc_chunk_next(struct m0_be_allocator *a,
						  struct be_alloc_chunk *c)
{
	struct be_alloc_chunk *r;

	r = chunks_all_tlist_next(&a->ba_h->bah_chunks.bl_list, c);
	M0_ASSERT(ergo(r != NULL, be_alloc_chunk_invariant(a, r)));
	return r;
}

static void be_alloc_chunk_mark_free(struct m0_be_allocator *a,
				     struct m0_be_tx *tx,
				     struct be_alloc_chunk *c)
{
	struct be_alloc_chunk *next;

	M0_PRE(be_alloc_chunk_invariant(a, c));
	M0_PRE(!c->bac_free);
	/* scan forward until free chunk found */
	for (next = c; next != NULL; next = be_alloc_chunk_next(a, next)) {
		if (next->bac_free)
			break;
	}
	c->bac_free = true;
	if (next == NULL)
		chunks_free_tlist_add_tail_c(a, tx, c);
	else
		chunks_free_tlist_add_before_c(a, tx, next, c);
	M0_POST(c->bac_free);
	M0_POST(be_alloc_chunk_invariant(a, c));
}

static uintptr_t be_alloc_chunk_after(struct m0_be_allocator *a,
				      struct be_alloc_chunk *c)
{
	return c == NULL ? (uintptr_t) a->ba_h->bah_addr :
			   (uintptr_t) &c->bac_mem[c->bac_size];
}

/** try to add a free chunk after the c */
static struct be_alloc_chunk *
be_alloc_chunk_add_after(struct m0_be_allocator *a,
			 struct m0_be_tx *tx,
			 struct be_alloc_chunk *c,
			 struct be_alloc_chunk *f,
			 uintptr_t offset,
			 m0_bcount_t size_total, bool free)
{
	struct be_alloc_chunk *new;

	M0_PRE(ergo(c != NULL, be_alloc_chunk_invariant(a, c)));
	M0_PRE(ergo(free && f != NULL, be_alloc_chunk_invariant(a, f)));
	M0_PRE(size_total > sizeof *new);

	new = c == NULL ? (struct be_alloc_chunk *)
			  ((uintptr_t) a->ba_h->bah_addr + offset) :
			  (struct be_alloc_chunk *) be_alloc_chunk_after(a, c);
	be_alloc_chunk_init(a, tx, new, size_total - sizeof(*new), free);

	/** add chunk to m0_be_allocator_header.bac_chunks list */
	if (c != NULL)
		chunks_all_tlist_add_after_c(a, tx, c, new);
	else
		chunks_all_tlist_add_c(a, tx, new);
	if (free) {
		/** add chunk to m0_be_allocator_header.bac_free list */
		if (f != NULL)
			chunks_free_tlist_add_after_c(a, tx, f, new);
		else
			chunks_free_tlist_add_c(a, tx, new);
	}
	M0_POST(be_alloc_chunk_invariant(a, new));
	M0_POST(ergo(free && f != NULL, be_alloc_chunk_invariant(a, f)));
	M0_PRE(ergo(c != NULL, be_alloc_chunk_invariant(a, c)));
	return new;
}

static struct be_alloc_chunk *
be_alloc_chunk_split(struct m0_be_allocator *a,
		     struct m0_be_tx *tx,
		     struct be_alloc_chunk *c,
		     uintptr_t start_new, m0_bcount_t size)
{
	struct be_alloc_chunk *prev;
	struct be_alloc_chunk *prev_free;
	struct be_alloc_chunk *new;
	const m0_bcount_t      chunk_size = sizeof *c;
	uintptr_t	       start0;
	uintptr_t	       start1;
	uintptr_t	       start_next;
	m0_bcount_t	       chunk0_size;
	m0_bcount_t	       chunk1_size;

	M0_PRE(be_alloc_chunk_invariant(a, c));
	M0_PRE(c->bac_free);

	prev	  = be_alloc_chunk_prev(a, c);
	prev_free = chunks_free_tlist_prev(&a->ba_h->bah_free.bl_list, c);

	start0	    = be_alloc_chunk_after(a, prev);
	start1	    = start_new + chunk_size + size;
	start_next  = be_alloc_chunk_after(a, c);
	chunk0_size = start_new - start0;
	chunk1_size = start_next - start1;
	M0_ASSERT(start0    <= start_new);
	M0_ASSERT(start_new <= start1);
	M0_ASSERT(start1    <= start_next);

	be_alloc_chunk_del_fini(a, tx, c);
	/* c is not a valid chunk now */

	if (chunk0_size <= chunk_size) {
		/* no space for chunk0 */
		if (prev != NULL)
			prev->bac_size += chunk0_size;
		else
			; /* space before the first chunk is temporary lost */
	} else {
		prev_free = be_alloc_chunk_add_after(a, tx, prev, prev_free,
						     0, chunk0_size, true);
		prev = prev_free;
	}

	/* add the new chunk */
	new = be_alloc_chunk_add_after(a, tx, prev, NULL,
				       prev == NULL ? chunk0_size : 0,
				       chunk_size + size, false);
	M0_ASSERT(new != NULL);

	if (chunk1_size <= chunk_size) {
		/* no space for chunk1 */
		new->bac_size += chunk1_size;
	} else {
		be_alloc_chunk_add_after(a, tx, new, prev_free,
					 0, chunk1_size, true);
	}
	/*
	 * XXX capture all chunks around the new in case if nearest chunks
	 * size was changed.
	 */
	chunks_all_tlist_capture_around(a, tx, new);

	M0_POST(!new->bac_free);
	M0_POST(new->bac_size >= size);
	M0_POST(be_alloc_chunk_invariant(a, new));
	return new;
}

static struct be_alloc_chunk *
be_alloc_chunk_trysplit(struct m0_be_allocator *a,
			struct m0_be_tx *tx,
			struct be_alloc_chunk *c,
			m0_bcount_t size, unsigned shift)
{
	struct be_alloc_chunk *result = NULL;
	uintptr_t	       alignment = 1UL << shift;
	uintptr_t	       addr_mem;
	uintptr_t	       addr_start;
	uintptr_t	       addr_end;
	const uintptr_t	       chunk_size = sizeof *c;

	M0_PRE(be_alloc_chunk_invariant(a, c));
	M0_PRE(alignment != 0);
	if (c->bac_free) {
		addr_start = (uintptr_t) c;
		addr_end   = (uintptr_t) &c->bac_mem[c->bac_size];
		/* find aligned address for memory block */
		addr_mem   = addr_start + chunk_size + alignment - 1;
		addr_mem  &= ~(alignment - 1);
		/* if block fits inside free chunk */
		result = addr_mem + size <= addr_end ?
			 be_alloc_chunk_split(a, tx, c, addr_mem - chunk_size,
					      size) : NULL;
	}
	M0_POST(ergo(result != NULL, be_alloc_chunk_invariant(a, result)));
	return result;
}

static bool be_alloc_chunk_trymerge(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct be_alloc_chunk *x,
				    struct be_alloc_chunk *y)
{
	m0_bcount_t y_size_total;
	bool	    chunks_were_merged = false;

	M0_PRE(ergo(x != NULL, be_alloc_chunk_invariant(a, x)));
	M0_PRE(ergo(y != NULL, be_alloc_chunk_invariant(a, y)));
	M0_PRE(ergo(x != NULL && y != NULL, (char *) x < (char *) y));
	M0_PRE(ergo(x != NULL, chunks_free_tlink_is_in(x)) ||
	       ergo(y != NULL, chunks_free_tlink_is_in(y)));
	if (x != NULL && y != NULL && x->bac_free && y->bac_free) {
		y_size_total = sizeof(*y) + y->bac_size;
		be_alloc_chunk_del_fini(a, tx, y);
		x->bac_size += y_size_total;
		be_alloc_chunk_capture(a, tx, x);
		chunks_were_merged = true;
	}
	M0_POST(ergo(x != NULL, be_alloc_chunk_invariant(a, x)));
	M0_POST(ergo(y != NULL && !chunks_were_merged,
		     be_alloc_chunk_invariant(a, y)));
	return chunks_were_merged;
}

M0_INTERNAL int m0_be_allocator_init(struct m0_be_allocator *a,
				     struct m0_be_seg *seg)
{
	M0_PRE(m0_be_seg__invariant(seg));

	m0_mutex_init(&a->ba_lock);

	a->ba_seg = seg;
	a->ba_h = &((struct m0_be_seg_hdr *) seg->bs_addr)->bh_alloc;
	M0_ASSERT(m0_addr_is_aligned(a->ba_h, BE_ALLOC_HEADER_SHIFT));

	return 0;
}

M0_INTERNAL void m0_be_allocator_fini(struct m0_be_allocator *a)
{
	m0_mutex_fini(&a->ba_lock);
}

M0_INTERNAL bool m0_be_allocator__invariant(struct m0_be_allocator *a)
{
	struct be_alloc_chunk *iter;
	bool		       success = true;

	m0_mutex_lock(&a->ba_lock);

	m0_tl_for(chunks_all, &a->ba_h->bah_chunks.bl_list, iter) {
		if (!be_alloc_chunk_invariant(a, iter)) {
			success = false;
			break;
		}
	} m0_tl_endfor;
	m0_tl_for(chunks_free, &a->ba_h->bah_free.bl_list, iter) {
		if (!be_alloc_chunk_invariant(a, iter) && !success) {
			success = false;
			break;
		}
	} m0_tl_endfor;

	m0_mutex_unlock(&a->ba_lock);

	return success;
}

M0_INTERNAL int m0_be_allocator_create(struct m0_be_allocator *a,
				       struct m0_be_tx *tx)
{
	struct m0_be_allocator_header *h;
	struct be_alloc_chunk	      *c;
	m0_bcount_t		       overhead;
	m0_bcount_t		       free_space;

	h = a->ba_h;
	/** @todo GET_PTR h */
	overhead   = a->ba_seg->bs_reserved;
	free_space = a->ba_seg->bs_size - overhead;

	/* check if segment is large enough to allocate at least 1 byte */
	if (a->ba_seg->bs_size <= overhead)
		return -ENOSPC;

	m0_mutex_lock(&a->ba_lock);

	h->bah_size = free_space;
	h->bah_addr = (void *) ((uintptr_t) a->ba_seg->bs_addr + overhead);

	chunks_all_tlist_init_c(a, tx, &h->bah_chunks.bl_list);
	chunks_free_tlist_init_c(a, tx, &h->bah_free.bl_list);

	h->bah_stats = (struct m0_be_allocator_stats) {
		.bas_free_space = free_space,
	};

	/* init main chunk */
	c = be_alloc_chunk_add_after(a, tx, NULL, NULL, 0, free_space, true);
	M0_ASSERT(c != NULL);

	m0_mutex_unlock(&a->ba_lock);

	/** @todo PUT_PTR h */

	M0_POST(m0_be_allocator__invariant(a));
	return 0;
}

M0_INTERNAL void m0_be_allocator_destroy(struct m0_be_allocator *a,
					 struct m0_be_tx *tx)
{
	struct m0_be_allocator_header *h;
	struct be_alloc_chunk	      *c;

	M0_PRE(m0_be_allocator__invariant(a));

	h = a->ba_h;
	c = chunks_all_tlist_head(&h->bah_chunks.bl_list);
	/** @todo GET_PTR h */
	m0_mutex_lock(&a->ba_lock);

	be_alloc_chunk_del_fini(a, tx, c);

	chunks_free_tlist_fini_c(a, tx, &h->bah_free.bl_list);
	chunks_all_tlist_fini_c(a, tx, &h->bah_chunks.bl_list);

	m0_mutex_unlock(&a->ba_lock);
	/** @todo PUT_PTR h */
}

M0_INTERNAL void m0_be_allocator_credit(struct m0_be_allocator *a,
					enum m0_be_allocator_op optype,
					m0_bcount_t             size,
					unsigned                shift,
					struct m0_be_tx_credit *accum)
{
	struct m0_be_tx_credit capture_around_credit = {};
	struct m0_be_tx_credit chunk_add_after_credit = {};
	struct m0_be_tx_credit chunk_del_fini_credit = {};
	struct m0_be_tx_credit chunk_trymerge_credit = {};
	struct m0_be_tx_credit mem_zero_credit = {};
	struct m0_be_tx_credit chunk_credit;
	struct m0_be_tx_credit header_credit;

	chunk_credit  = M0_BE_TX_CREDIT_TYPE(struct be_alloc_chunk);
	header_credit = M0_BE_TX_CREDIT_TYPE(struct m0_be_allocator_header);

	shift = max_check(shift, (unsigned) M0_BE_ALLOC_SHIFT_MIN);
	mem_zero_credit = M0_BE_TX_CREDIT(1, size * 2);

	m0_be_tx_credit_add(&capture_around_credit, &header_credit);
	m0_be_tx_credit_mac(&capture_around_credit, &chunk_credit, 3);

	/* tlink_init() x2 */
	m0_be_tx_credit_mac(&chunk_add_after_credit, &chunk_credit, 2);
	/* tlist_add_after() x2 */
	m0_be_tx_credit_mac(&chunk_add_after_credit, &capture_around_credit, 4);

	/* tlist_del() x2 */
	m0_be_tx_credit_mac(&chunk_del_fini_credit, &capture_around_credit, 2);
	/* tlink_fini() x2 */
	m0_be_tx_credit_mac(&chunk_del_fini_credit, &chunk_credit, 2);

	m0_be_tx_credit_add(&chunk_trymerge_credit, &chunk_del_fini_credit);
	m0_be_tx_credit_add(&chunk_trymerge_credit, &chunk_credit);

	/** @todo TODO XXX add list credits instead of entire header */
	switch (optype) {
		case M0_BAO_CREATE:
			/* tlist_init x2 */
			m0_be_tx_credit_mac(accum, &header_credit, 2);
			m0_be_tx_credit_add(accum, &chunk_add_after_credit);
			break;
		case M0_BAO_DESTROY:
			m0_be_tx_credit_add(accum, &chunk_del_fini_credit);
			/* tlist_fini x2 */
			m0_be_tx_credit_mac(accum, &header_credit, 2);
			break;
		case M0_BAO_ALLOC_ALIGNED:
			m0_be_tx_credit_add(accum, &chunk_del_fini_credit);
			m0_be_tx_credit_mac(accum, &chunk_add_after_credit, 3);
			m0_be_tx_credit_add(accum, &capture_around_credit);
			m0_be_tx_credit_add(accum, &mem_zero_credit);
			break;
		case M0_BAO_ALLOC:
			m0_be_allocator_credit(a, M0_BAO_ALLOC_ALIGNED, size,
					       M0_BE_ALLOC_SHIFT_MIN, accum);
			break;
		case M0_BAO_FREE_ALIGNED:
			/* be_alloc_chunk_mark_free = tlist_add_before() */
			m0_be_tx_credit_mac(accum, &capture_around_credit, 2);
			m0_be_tx_credit_mac(accum, &chunk_trymerge_credit, 2);
			break;
		case M0_BAO_FREE:
			m0_be_allocator_credit(a, M0_BAO_FREE_ALIGNED, size,
					       shift, accum);
			break;
		default:
			M0_IMPOSSIBLE("Invalid allocator operation type");
	}
}

M0_INTERNAL void m0_be_alloc_aligned(struct m0_be_allocator *a,
				     struct m0_be_tx *tx,
				     struct m0_be_op *op,
				     void **ptr,
				     m0_bcount_t size,
				     unsigned shift)
{
	struct be_alloc_chunk *iter;
	struct be_alloc_chunk *c = NULL;

	M0_PRE(m0_be_allocator__invariant(a));
	shift = max_check(shift, (unsigned) M0_BE_ALLOC_SHIFT_MIN);

	/* XXX */
	m0_be_op_state_set(op, M0_BOS_ACTIVE);

	m0_mutex_lock(&a->ba_lock);
	/* algorithm starts here */
	m0_tl_for(chunks_free, &a->ba_h->bah_free.bl_list, iter) {
		c = be_alloc_chunk_trysplit(a, tx, iter, size, shift);
		if (c != NULL)
			break;
	} m0_tl_endfor;
	if (c != NULL && tx != NULL) {
		memset(&c->bac_mem, 0, c->bac_size);
		m0_be_tx_capture(tx, &M0_BE_REG(a->ba_seg,
						c->bac_size, &c->bac_mem));
	}
	op->bo_u.u_allocator.a_ptr = c == NULL ?    NULL : &c->bac_mem;
	op->bo_u.u_allocator.a_rc  = c == NULL ? -ENOMEM : 0;
	if (ptr != NULL)
		*ptr = op->bo_u.u_allocator.a_ptr;
	/* and ends here */
	if (c != NULL) {
		M0_POST(!c->bac_free);
		M0_POST(c->bac_size >= size);
		M0_POST(m0_addr_is_aligned(&c->bac_mem, shift));
		M0_POST(be_alloc_is_chunk_in_allocator(a, c));
	}
	/*
	 * unlock mutex after post-conditions which are using allocator
	 * internals
	 */
	m0_mutex_unlock(&a->ba_lock);

	M0_POST(m0_be_allocator__invariant(a));
	M0_POST(equi(op->bo_u.u_allocator.a_ptr != NULL,
		     op->bo_u.u_allocator.a_rc == 0));

	/* set op state after post-conditions because they are using op */
	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}

M0_INTERNAL void m0_be_alloc(struct m0_be_allocator *a,
			     struct m0_be_tx *tx,
			     struct m0_be_op *op,
			     void **ptr,
			     m0_bcount_t size)
{
	m0_be_alloc_aligned(a, tx, op, ptr, size, M0_BE_ALLOC_SHIFT_MIN);
}

M0_INTERNAL void m0_be_free_aligned(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op,
				    void *ptr)
{
	struct be_alloc_chunk *c;
	struct be_alloc_chunk *prev;
	struct be_alloc_chunk *next;
	bool		       chunks_were_merged;

	M0_PRE(m0_be_allocator__invariant(a));
	M0_PRE(ergo(ptr != NULL, be_alloc_is_mem_in_allocator(a, 1, ptr)));

	m0_be_op_state_set(op, M0_BOS_ACTIVE);

	if (ptr != NULL) {
		m0_mutex_lock(&a->ba_lock);

		c = be_alloc_chunk_addr(ptr);
		M0_PRE(be_alloc_chunk_invariant(a, c));
		M0_PRE(!c->bac_free);
		/* algorithm starts here */
		be_alloc_chunk_mark_free(a, tx, c);
		prev = be_alloc_chunk_prev(a, c);
		next = be_alloc_chunk_next(a, c);
		chunks_were_merged = be_alloc_chunk_trymerge(a, tx, prev, c);
		if (chunks_were_merged)
			c = prev;
		be_alloc_chunk_trymerge(a, tx, c, next);
		/* and ends here */
		M0_POST(c->bac_free);
		M0_POST(c->bac_size > 0);
		M0_POST(be_alloc_chunk_invariant(a, c));

		m0_mutex_unlock(&a->ba_lock);
	}

	m0_be_op_state_set(op, M0_BOS_SUCCESS);

	M0_POST(m0_be_allocator__invariant(a));
}

M0_INTERNAL void m0_be_free(struct m0_be_allocator *a,
			    struct m0_be_tx *tx,
			    struct m0_be_op *op,
			    void *ptr)
{
	m0_be_free_aligned(a, tx, op, ptr);
}

M0_INTERNAL void m0_be_alloc_stats(struct m0_be_allocator *a,
				   struct m0_be_allocator_stats *out)
{
	M0_PRE(m0_be_allocator__invariant(a));

	/** @todo GET_PTR a->ba_h */
	m0_mutex_lock(&a->ba_lock);
	*out = a->ba_h->bah_stats;
	m0_mutex_unlock(&a->ba_lock);
	/** @todo PUT_PTR a->ba_h */
}

/** @} end of be group */
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
