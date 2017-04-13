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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 29-May-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/alloc.h"
#include "be/alloc_internal.h"
#include "be/seg_internal.h"    /* m0_be_seg_hdr */
#include "be/tx.h"              /* M0_BE_TX_CAPTURE_PTR */
#include "be/op.h"              /* m0_be_op */
#include "lib/memory.h"         /* m0_addr_is_aligned */
#include "lib/errno.h"          /* ENOSPC */
#include "lib/misc.h"		/* memset, M0_BITS */
#include "mero/magic.h"
#include "be/domain.h"		/* m0_be_domain */

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
 *
 * Space reservation for DIX recovery
 * ----------------------------------
 *
 * Space is reserved to not get -ENOSPC during DIX repair.
 *
 * A special allocator zone M0_BAP_REPAIR is introduced for a memory allocated
 * during DIX repair. This zone is specified in zone mask in
 * m0_be_alloc_aligned() and its callers up to functions which may be called
 * during repair (from m0_dix_cm_cp_write()). Functions/macro which are never
 * called during repair pass always M0_BITS(M0_BAP_NORMAL) as zone mask.
 *
 * Repair uses all available space in the allocator while normal alloc fails
 * if free space is less than reserved. Repair uses M0_BAP_REPAIR zone by
 * default, but if there is no space in M0_BAP_REPAIR, then memory will be
 * allocated in M0_BAP_NORMAL zone.
 *
 * The space is reserved for repair zone during m0mkfs in
 * m0_be_allocator_create(). Percentage of free space is passed to
 * m0_be_allocator_create() via 'zone_pcnt' argument which is assigned in
 * cs_be_init(). The space is reserved in terms of bytes, not memory region, so
 * fragmentation can prevent successful allocation from reserved space if there
 * is no contiguous memory block with requested size.
 *
 * Repair zone is not accounted in df output.
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

static const char *be_alloc_zone_name(enum m0_be_alloc_zone_type type)
{
	static const char *zone_names[] = {
		[M0_BAP_REPAIR] = "repair",
		[M0_BAP_NORMAL] = "normal"
	};

	M0_CASSERT(ARRAY_SIZE(zone_names) == M0_BAP_NR);
	return zone_names[type];
}

static void
be_allocator_call_stat_init(struct m0_be_allocator_call_stat *cstat)
{
	*cstat = (struct m0_be_allocator_call_stat){
		.bcs_nr   = 0,
		.bcs_size = 0,
	};
}

static void be_allocator_call_stats_init(struct m0_be_allocator_call_stats *cs)
{
	be_allocator_call_stat_init(&cs->bacs_alloc_success);
	be_allocator_call_stat_init(&cs->bacs_alloc_failure);
	be_allocator_call_stat_init(&cs->bacs_free);
}

static void be_allocator_stats_init(struct m0_be_allocator_stats  *stats,
				    struct m0_be_allocator_header *h)
{
#ifdef ENABLE_BE_ALLOC_ZONES
	struct m0_be_alloc_zone *z;
	int                      i;
#endif

	*stats = (struct m0_be_allocator_stats){
		.bas_chunk_overhead = sizeof(struct be_alloc_chunk),
		.bas_stat0_boundary = M0_BE_ALLOCATOR_STATS_BOUNDARY,
		.bas_print_interval = M0_BE_ALLOCATOR_STATS_PRINT_INTERVAL,
		.bas_print_index    = 0,
	};
#ifdef ENABLE_BE_ALLOC_ZONES
	for (i = 0; i < M0_BAP_NR; i++) {
		z = &h->bah_zone[i];
		stats->bas_zones[i] = (struct m0_be_alloc_zone_stats) {
			.bzs_total = z->baz_size,
			.bzs_used  = z->baz_size - z->baz_free,
			.bzs_free  = z->baz_free,
			.bzs_type  = i
		};
		M0_ASSERT(be_alloc_zone_name(i) != NULL);
		be_allocator_call_stats_init(&stats->bas_zones[i].bzs_stats);
	}
#endif
	be_allocator_call_stats_init(&stats->bas_stat0);
	be_allocator_call_stats_init(&stats->bas_stat1);
}

static void
be_allocator_call_stat_update(struct m0_be_allocator_call_stat *cstat,
                              unsigned long                     nr,
                              m0_bcount_t                       size)
{
	cstat->bcs_nr   += nr;
	cstat->bcs_size += size;
}

static void
be_allocator_call_stats_update(struct m0_be_allocator_call_stats *cs,
			       m0_bcount_t                        size,
			       bool                               alloc,
			       bool                               failed)
{
	struct m0_be_allocator_call_stat *cstat;
	if (alloc && failed) {
		cstat = &cs->bacs_alloc_failure;
	} else if (alloc) {
		cstat = &cs->bacs_alloc_success;
	} else {
		cstat = &cs->bacs_free;
	}
	be_allocator_call_stat_update(cstat, 1, size);
}

static void
be_allocator_call_stats_print(struct m0_be_allocator_call_stats *cs,
                              const char                        *descr)
{
#define P_ACS(acs) (acs)->bcs_nr, (acs)->bcs_size
	M0_LOG(M0_DEBUG, "%s (nr, size): alloc_success=(%lu, %lu), "
	       "free=(%lu, %lu), alloc_failure=(%lu, %lu)", descr,
	       P_ACS(&cs->bacs_alloc_success), P_ACS(&cs->bacs_free),
	       P_ACS(&cs->bacs_alloc_failure));
#undef P_ACS
}

M0_UNUSED
static void be_allocator_zone_stats_print(struct m0_be_alloc_zone_stats *zstats)
{
	M0_LOG(M0_DEBUG, "zone_name=%s total=%lu used=%lu free=%lu",
	       be_alloc_zone_name(zstats->bzs_type), zstats->bzs_total,
	       zstats->bzs_used, zstats->bzs_free);
	be_allocator_call_stats_print(&zstats->bzs_stats,
				      be_alloc_zone_name(zstats->bzs_type));
}

static void be_allocator_stats_print(struct m0_be_allocator_stats *stats)
{
#ifdef ENABLE_BE_ALLOC_ZONES
	int i;
#endif

	M0_LOG(M0_DEBUG, "stats=%p chunk_overhead=%lu boundary=%lu "
	       "print_interval=%lu print_index=%lu",
	       stats, stats->bas_chunk_overhead, stats->bas_stat0_boundary,
	       stats->bas_print_interval, stats->bas_print_index);
#ifdef ENABLE_BE_ALLOC_ZONES
	for (i = 0; i < M0_BAP_NR; i++)
		be_allocator_zone_stats_print(&stats->bas_zones[i]);
#else
	M0_LOG(M0_DEBUG, "chunks=%lu free_chunks=%lu",
	       stats->bas_chunks_nr, stats->bas_free_chunks_nr);
	be_allocator_call_stats_print(&stats->bas_total, "           total");
#endif
	be_allocator_call_stats_print(&stats->bas_stat0, "size <= boundary");
	be_allocator_call_stats_print(&stats->bas_stat1, "size >  boundary");
}

static void be_allocator_stats_update(struct m0_be_allocator_stats *stats,
                                      m0_bcount_t                   size,
                                      bool                          alloc,
				      bool                          failed,
				      enum m0_be_alloc_zone_type    zone_type)
{
#ifdef ENABLE_BE_ALLOC_ZONES
	unsigned long space_change;
	long          multiplier;
#endif

	M0_PRE(ergo(failed, alloc));

#ifdef ENABLE_BE_ALLOC_ZONES
	multiplier   = failed ? 0 : alloc ? 1 : -1;
	space_change = size + stats->bas_chunk_overhead;
	stats->bas_zones[zone_type].bzs_used += multiplier * space_change;
	stats->bas_zones[zone_type].bzs_free -= multiplier * space_change;
	be_allocator_call_stats_update(&stats->bas_zones[zone_type].bzs_stats,
				       size, alloc, failed);
#else
	be_allocator_call_stats_update(&stats->bas_total, size, alloc, failed);
#endif
	be_allocator_call_stats_update(size <= stats->bas_stat0_boundary ?
				       &stats->bas_stat0 : &stats->bas_stat1,
				       size, alloc, failed);
	if (stats->bas_print_index++ == stats->bas_print_interval) {
		be_allocator_stats_print(stats);
		stats->bas_print_index = 0;
	}
}

static void be_alloc_chunk_capture(struct m0_be_allocator *a,
				   struct m0_be_tx *tx,
				   struct be_alloc_chunk *c)
{
	if (tx != NULL && c != NULL)
		M0_BE_TX_CAPTURE_PTR(a->ba_seg, tx, c);
}

static void be_alloc_head_capture(struct m0_be_allocator *a,
				  struct m0_be_tx *tx)
{
	if (tx != NULL)
		M0_BE_TX_CAPTURE_PTR(a->ba_seg, tx, a->ba_h);
}

static void be_alloc_list_capture(struct m0_be_list *list,
				  struct m0_be_seg *seg,
				  struct m0_be_tx *tx)
{
	if (tx != NULL)
		M0_BE_TX_CAPTURE_PTR(seg, tx, list);
}

static void be_alloc_flist_capture(struct m0_tl *list,
				   struct m0_be_seg *seg,
				   struct m0_be_tx *tx)
{
	if (tx != NULL)
		M0_BE_TX_CAPTURE_PTR(seg, tx, list);
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
	be_alloc_list_capture(&a->ba_h->bah_chunks, a->ba_seg, tx);
}

static struct m0_tl* bah_fl_s(struct m0_be_allocator *a, m0_bcount_t sz)
{
	return &a->ba_h->bah_free[(sz >> M0_BE_ALLOC_SHIFT_MIN) % BAH_FREE_NR];
}

static struct m0_tl* bah_fl(struct m0_be_allocator *a,
			    const struct be_alloc_chunk *c)
{
	/*
	 * Last Big Chunk is always at list[0] regardless of its size.
	 * This allows to locate it easy.
	 */
	if (&c->bac_mem[c->bac_size] == a->ba_h->bah_addr + a->ba_h->bah_size)
		return bah_fl_s(a, 0);
	else
		return bah_fl_s(a, c->bac_size);
}

static void chunks_free_tlist_capture_around(struct m0_be_allocator *a,
					     struct m0_be_tx *tx,
					     struct be_alloc_chunk *c)
{
	struct be_alloc_chunk *fprev;
	struct be_alloc_chunk *fnext;

	fprev = chunks_free_tlist_prev(bah_fl(a, c), c);
	fnext = chunks_free_tlist_next(bah_fl(a, c), c);
	be_alloc_chunk_capture(a, tx, c);
	be_alloc_chunk_capture(a, tx, fprev);
	be_alloc_chunk_capture(a, tx, fnext);
	be_alloc_flist_capture(bah_fl(a, c), a->ba_seg, tx);
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

	a->ba_h->bah_stats.bas_chunks_nr--;
	cprev = chunks_all_tlist_prev(&a->ba_h->bah_chunks.bl_list, c);
	cnext = chunks_all_tlist_next(&a->ba_h->bah_chunks.bl_list, c);
	chunks_all_tlist_del(c);
	be_alloc_chunk_capture(a, tx, cprev);
	be_alloc_chunk_capture(a, tx, cnext);
	be_alloc_list_capture(&a->ba_h->bah_chunks, a->ba_seg, tx);
}

static void chunks_free_tlist_del_c(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct be_alloc_chunk *c)
{
	struct be_alloc_chunk *fprev;
	struct be_alloc_chunk *fnext;

	a->ba_h->bah_stats.bas_free_chunks_nr--;
	fprev = chunks_free_tlist_prev(bah_fl(a, c), c);
	fnext = chunks_free_tlist_next(bah_fl(a, c), c);
	chunks_free_tlist_del(c);
	be_alloc_chunk_capture(a, tx, fprev);
	be_alloc_chunk_capture(a, tx, fnext);
	be_alloc_flist_capture(bah_fl(a, c), a->ba_seg, tx);
}

static void chunks_free_tlist_add_c(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct be_alloc_chunk *c)
{
	a->ba_h->bah_stats.bas_free_chunks_nr++;
	chunks_free_tlist_add(bah_fl(a, c), c);
	chunks_free_tlist_capture_around(a, tx, c);
}

static void chunks_all_tlist_add_after_c(struct m0_be_allocator *a,
					 struct m0_be_tx *tx,
					 struct be_alloc_chunk *c,
					 struct be_alloc_chunk *new)
{
	a->ba_h->bah_stats.bas_chunks_nr++;
	chunks_all_tlist_add_after(c, new);
	chunks_all_tlist_capture_around(a, tx, c);
	chunks_all_tlist_capture_around(a, tx, new);
}

static void chunks_all_tlist_add_c(struct m0_be_allocator *a,
				   struct m0_be_tx *tx,
				   struct be_alloc_chunk *new)
{
	a->ba_h->bah_stats.bas_chunks_nr++;
	chunks_all_tlist_add(&a->ba_h->bah_chunks.bl_list, new);
	chunks_all_tlist_capture_around(a, tx, new);
}

static void chunks_all_tlist_init_c(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct m0_tl *l)
{
	chunks_all_tlist_init(l);
	be_alloc_head_capture(a, tx);
}

static void chunks_all_tlist_fini_c(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct m0_tl *l)
{
	chunks_all_tlist_fini(l);
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
#if 0
	return a == NULL || b == NULL ||
	       (a < b && &a->bac_mem[a->bac_size] <= (char *) b);
#else
	return a == NULL || b == NULL ||
	       (a < b && &a->bac_mem[a->bac_size] <= (char *) b) ||
	       (b < a && &b->bac_mem[b->bac_size] <= (char *) a);
#endif
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
		fprev = chunks_free_tlist_prev(bah_fl(a, c), c);
		fnext = chunks_free_tlist_next(bah_fl(a, c), c);
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
	M0_ASSERT_EX(ergo(r != NULL, be_alloc_chunk_invariant(a, r)));
	return r;
}

static void be_alloc_chunk_mark_free(struct m0_be_allocator *a,
				     struct m0_be_tx *tx,
				     struct be_alloc_chunk *c)
{
	M0_PRE(be_alloc_chunk_invariant(a, c));
	M0_PRE(!c->bac_free);
	/* don't maintain order of chunks in the free chunks list */
#if 0
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
#else
	c->bac_free = true;
	chunks_free_tlist_add_c(a, tx, c);
#endif
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
			 uintptr_t offset,
			 m0_bcount_t size_total, bool free)
{
	struct be_alloc_chunk *new;

	M0_PRE(ergo(c != NULL, be_alloc_chunk_invariant(a, c)));
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
	if (free)
			chunks_free_tlist_add_c(a, tx, new);
	M0_POST(be_alloc_chunk_invariant(a, new));
	M0_POST(ergo(c != NULL, be_alloc_chunk_invariant(a, c)));
	return new;
}

static void be_alloc_chunk_size_inc(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct be_alloc_chunk *c,
				    m0_bcount_t sz)
{
	c->bac_size += sz;
	if (c->bac_free) {
		chunks_free_tlist_del_c(a, tx, c);
		chunks_free_tlist_add_c(a, tx, c);
	}
}

static struct be_alloc_chunk *
be_alloc_chunk_split(struct m0_be_allocator *a,
		     struct m0_be_tx *tx,
		     struct be_alloc_chunk *c,
		     uintptr_t start_new, m0_bcount_t size,
		     enum m0_be_alloc_zone_type zone)
{
	struct be_alloc_chunk *prev;
	struct be_alloc_chunk *new;
	const m0_bcount_t      hdr_size = sizeof *c;
	uintptr_t	       start0;
	uintptr_t	       start1;
	uintptr_t	       start_next;
	m0_bcount_t	       chunk0_size;
	m0_bcount_t	       chunk1_size;

	M0_PRE(be_alloc_chunk_invariant(a, c));
	M0_PRE(c->bac_free);

	prev	  = be_alloc_chunk_prev(a, c);

	start0	    = be_alloc_chunk_after(a, prev);
	start1	    = start_new + hdr_size + size;
	start_next  = be_alloc_chunk_after(a, c);
	chunk0_size = start_new - start0;
	chunk1_size = start_next - start1;
	M0_ASSERT(start0    <= start_new);
	M0_ASSERT(start_new <= start1);
	M0_ASSERT(start1    <= start_next);

	be_alloc_chunk_del_fini(a, tx, c);
	/* c is not a valid chunk now */

	if (chunk0_size <= hdr_size) {
		/* no space for chunk0 */
		if (prev != NULL)
			be_alloc_chunk_size_inc(a, tx, prev, chunk0_size);
		else
			; /* space before the first chunk is temporary lost */
	} else {
		prev = be_alloc_chunk_add_after(a, tx, prev,
						0, chunk0_size, true);
	}

	M0_LOG(M0_DEBUG, "c0=%lu cn=%lu c1=%lu",
	       chunk0_size, hdr_size + size, chunk1_size);

	/* add the new chunk */
	new = be_alloc_chunk_add_after(a, tx, prev,
				       prev == NULL ? chunk0_size : 0,
				       hdr_size + size, false);
	M0_ASSERT(new != NULL);

	if (chunk1_size <= hdr_size) {
		/* no space for chunk1 */
		new->bac_size += chunk1_size;
	} else {
		be_alloc_chunk_add_after(a, tx, new,
					 0, chunk1_size, true);
	}
#ifdef ENABLE_BE_ALLOC_ZONES
	new->bac_zone = zone;
#endif
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
			m0_bcount_t size, unsigned shift,
			enum m0_be_alloc_zone_type zone)
{
	struct be_alloc_chunk *result = NULL;
	uintptr_t	       alignment = 1UL << shift;
	uintptr_t	       addr_mem;
	uintptr_t	       addr_start;
	uintptr_t	       addr_end;
	const uintptr_t	       hdr_size = sizeof *c;

	M0_PRE_EX(be_alloc_chunk_invariant(a, c));
	M0_PRE(alignment != 0);
	if (c->bac_free) {
		addr_start = (uintptr_t) c;
		addr_end   = (uintptr_t) &c->bac_mem[c->bac_size];
		addr_mem   = m0_align(addr_start + hdr_size, alignment);
		/* if block fits inside free chunk */
		result = addr_mem + size <= addr_end ?
			 be_alloc_chunk_split(a, tx, c, addr_mem - hdr_size,
					      size, zone) : NULL;
	}
	M0_POST(ergo(result != NULL, be_alloc_chunk_invariant(a, result)));
	return result;
}

static bool be_alloc_chunk_trymerge(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct be_alloc_chunk *x,
				    struct be_alloc_chunk *y)
{
	m0_bcount_t size;

	M0_PRE(ergo(x != NULL, be_alloc_chunk_invariant(a, x)));
	M0_PRE(ergo(y != NULL, be_alloc_chunk_invariant(a, y)));

	if (x == NULL || y == NULL || !x->bac_free || !y->bac_free)
		return false;

	M0_PRE(chunks_free_tlink_is_in(x) && chunks_free_tlink_is_in(y));
	M0_PRE(be_alloc_chunk_next(a, x) == y &&
	       be_alloc_chunk_prev(a, y) == x);

	size = sizeof(*y) + y->bac_size;
	be_alloc_chunk_del_fini(a, tx, y);
	be_alloc_chunk_size_inc(a, tx, x, size);

	M0_POST(be_alloc_chunk_invariant(a, x));

	return true;
}

M0_INTERNAL int m0_be_allocator_init(struct m0_be_allocator *a,
				     struct m0_be_seg *seg)
{
	M0_ENTRY("a=%p seg=%p seg->bs_addr=%p seg->bs_size=%lu",
		 a, seg, seg->bs_addr, seg->bs_size);

	M0_PRE(m0_be_seg__invariant(seg));

	a->ba_trymerge = false;

	m0_mutex_init(&a->ba_lock);

	a->ba_seg = seg;
	a->ba_h = &((struct m0_be_seg_hdr *) seg->bs_addr)->bh_alloc;
	M0_ASSERT(m0_addr_is_aligned(a->ba_h, BE_ALLOC_HEADER_SHIFT));

#ifdef ENABLE_BE_ALLOC_ZONES
	M0_LOG(M0_DEBUG,
	       "a=%p rz_size %"PRIu64" nz_free %"PRIu64,
	       a,
	       a->ba_h->bah_zone[M0_BAP_REPAIR].baz_size,
	       a->ba_h->bah_zone[M0_BAP_NORMAL].baz_free);
#endif
	return 0;
}

M0_INTERNAL void m0_be_allocator_fini(struct m0_be_allocator *a)
{
	M0_ENTRY("a=%p", a);
	be_allocator_stats_print(&a->ba_h->bah_stats);
	m0_mutex_fini(&a->ba_lock);
	M0_LEAVE();
}

M0_INTERNAL bool m0_be_allocator__invariant(struct m0_be_allocator *a)
{
	bool success = true;
	/* XXX Disabled as it's too slow. */
	if (0) {
		m0_mutex_lock(&a->ba_lock);

		success = m0_tl_forall(chunks_all, iter,
				       &a->ba_h->bah_chunks.bl_list,
				       be_alloc_chunk_invariant(a, iter)) &&
			m0_tl_forall(chunks_free, iter,
				     &a->ba_h->bah_free[0],
				     be_alloc_chunk_invariant(a, iter));

		m0_mutex_unlock(&a->ba_lock);
	}
	return success;
}

/**
 * Distribute segment space between allocator memory zones.
 *
 * User is able to define percentage of space occupied by every zone.
 */
M0_UNUSED
static void be_allocator_zones_init(struct m0_be_alloc_zone *zones,
				    m0_bcount_t              total_size,
				    uint32_t                *zone_pcnt,
				    uint32_t                 zones_nr)
{
	uint32_t i;
	uint32_t biggest;

	M0_PRE(zones_nr == M0_BAP_NR);
	M0_PRE(m0_forall(i, zones_nr, zone_pcnt[i] <= 100));
	M0_PRE(m0_reduce(i, zones_nr, 0, + zone_pcnt[i]) == 100);

	/* Find memory zone with the biggest percentage. */
	biggest = 0;
	for (i = 1; i < zones_nr; i++)
		if (zone_pcnt[i] > zone_pcnt[biggest])
			biggest = i;
	M0_ASSERT(biggest < M0_BAP_NR);
	/* First fill the size of all zones, except the biggest one. */
	for (i = 0; i < zones_nr; i++) {
		if (i != biggest) {
			zones[i].baz_size = total_size * zone_pcnt[i] / 100;
			zones[i].baz_free = zones[i].baz_size;
			total_size -= zones[i].baz_size;
		}
	}
	/* All remained space is for the biggest zone. */
	zones[biggest].baz_size = zones[biggest].baz_free = total_size;
}

M0_INTERNAL int m0_be_allocator_create(struct m0_be_allocator *a,
				       struct m0_be_tx        *tx,
				       uint32_t               *zone_pcnt,
				       uint32_t                zones_nr)
{
	int                            i;
	struct m0_be_allocator_header *h;
	struct be_alloc_chunk         *c;
	m0_bcount_t                    overhead;
	m0_bcount_t                    free_space;

	M0_ENTRY("a=%p tx=%p", a, tx);
	h = a->ba_h;
	/** @todo GET_PTR h */
	overhead   = a->ba_seg->bs_reserved;
	free_space = a->ba_seg->bs_size - overhead;

	/* check if segment is large enough to allocate at least 1 byte */
	if (a->ba_seg->bs_size <= overhead)
		return M0_ERR(-ENOSPC);

	m0_mutex_lock(&a->ba_lock);

	h->bah_size = free_space;
	h->bah_addr = (void *) ((uintptr_t) a->ba_seg->bs_addr + overhead);
#ifdef ENABLE_BE_ALLOC_ZONES
	be_allocator_zones_init(h->bah_zone, free_space, zone_pcnt, zones_nr);
	M0_ASSERT(m0_reduce(i, ARRAY_SIZE(h->bah_zone), 0ULL,
			    + h->bah_zone[i].baz_size) == h->bah_size);
	M0_LOG(M0_DEBUG, "bah_size=%"PRIu64" normal_zone_size=%"PRIu64
	       " repair_zone_size=%"PRIu64,
	       h->bah_size, h->bah_zone[M0_BAP_REPAIR].baz_size,
	       h->bah_zone[M0_BAP_NORMAL].baz_size);
#endif

	chunks_all_tlist_init_c(a, tx, &h->bah_chunks.bl_list);
	for (i = 0; i < BAH_FREE_NR; i++)
		chunks_free_tlist_init(&h->bah_free[i]);

	be_allocator_stats_init(&h->bah_stats, h);

	/* init main chunk */
	c = be_alloc_chunk_add_after(a, tx, NULL, 0, free_space, true);
	M0_ASSERT(c != NULL);

	be_alloc_head_capture(a, tx);

	m0_mutex_unlock(&a->ba_lock);

	/** @todo PUT_PTR h */

	M0_POST_EX(m0_be_allocator__invariant(a));
	M0_LEAVE();
	return 0;
}

M0_INTERNAL void m0_be_allocator_destroy(struct m0_be_allocator *a,
					 struct m0_be_tx *tx)
{
	int                            i;
	struct m0_be_allocator_header *h;
	struct be_alloc_chunk	      *iter;

	M0_ENTRY("a=%p tx=%p", a, tx);

	M0_PRE_EX(m0_be_allocator__invariant(a));

	h = a->ba_h;
	/** @todo GET_PTR h */
	m0_mutex_lock(&a->ba_lock);

	m0_tl_for(chunks_all, &h->bah_chunks.bl_list, iter) {
		chunks_free_tlist_del(iter);
		chunks_free_tlink_fini(iter);
		chunks_all_tlist_del(iter);
		chunks_all_tlink_fini(iter);
	} m0_tl_endfor;

	for (i = 0; i < BAH_FREE_NR; i++)
		chunks_free_tlist_fini(&h->bah_free[i]);

	chunks_all_tlist_fini_c(a, tx, &h->bah_chunks.bl_list);

	m0_mutex_unlock(&a->ba_lock);
	/** @todo PUT_PTR h */
	M0_LEAVE();
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
	struct m0_be_tx_credit chunk_size_inc_credit = {};
	struct m0_be_tx_credit mem_zero_credit = {};
	struct m0_be_tx_credit chunk_credit;
	struct m0_be_tx_credit header_credit;
	struct m0_be_tx_credit list_credit;

	chunk_credit  = M0_BE_TX_CREDIT_TYPE(struct be_alloc_chunk);
	header_credit = M0_BE_TX_CREDIT_TYPE(struct m0_be_allocator_header);
	list_credit   = M0_BE_TX_CREDIT_TYPE(struct m0_be_list);

	shift = max_check(shift, (unsigned) M0_BE_ALLOC_SHIFT_MIN);
	mem_zero_credit = M0_BE_TX_CREDIT(1, size * 2);

	m0_be_tx_credit_add(&capture_around_credit, &list_credit);
	m0_be_tx_credit_mac(&capture_around_credit, &chunk_credit, 3);

	/* tlink_init() x2 */
	m0_be_tx_credit_mac(&chunk_add_after_credit, &chunk_credit, 2);
	/* tlist_add_after() x2 */
	m0_be_tx_credit_mac(&chunk_add_after_credit, &capture_around_credit, 4);

	/* tlist_del() x2 */
	m0_be_tx_credit_mac(&chunk_del_fini_credit, &capture_around_credit, 2);
	/* tlink_fini() x2 */
	m0_be_tx_credit_mac(&chunk_del_fini_credit, &chunk_credit, 2);

	m0_be_tx_credit_mac(&chunk_size_inc_credit, &capture_around_credit, 2);

	m0_be_tx_credit_add(&chunk_trymerge_credit, &chunk_del_fini_credit);
	m0_be_tx_credit_add(&chunk_trymerge_credit, &chunk_size_inc_credit);

	/** @todo TODO XXX add list credits instead of entire header */
	switch (optype) {
		case M0_BAO_CREATE:
			/* allocator header */
			m0_be_tx_credit_add(accum, &header_credit);
			/* tlist_init x2 */
			m0_be_tx_credit_mac(accum, &header_credit, 2);
			m0_be_tx_credit_add(accum, &chunk_add_after_credit);
			break;
		case M0_BAO_DESTROY:
			m0_be_tx_credit_mac(accum, &header_credit, 1);
			break;
		case M0_BAO_ALLOC_ALIGNED:
			m0_be_tx_credit_add(accum, &header_credit);
			m0_be_tx_credit_add(accum, &chunk_del_fini_credit);
			m0_be_tx_credit_add(accum, &chunk_size_inc_credit);
			m0_be_tx_credit_mac(accum, &chunk_add_after_credit, 3);
			m0_be_tx_credit_add(accum, &capture_around_credit);
			m0_be_tx_credit_add(accum, &mem_zero_credit);
			break;
		case M0_BAO_ALLOC:
			m0_be_allocator_credit(a, M0_BAO_ALLOC_ALIGNED, size,
					       M0_BE_ALLOC_SHIFT_MIN, accum);
			break;
		case M0_BAO_FREE_ALIGNED:
			m0_be_tx_credit_add(accum, &header_credit);
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
				     unsigned shift,
				     uint64_t zonemask)
{
	int                            iter_nr = 0;
	struct be_alloc_chunk         *iter;
	struct be_alloc_chunk         *c = NULL;
	enum  m0_be_alloc_zone_type    ztype = M0_BAP_NORMAL;
#ifdef ENABLE_BE_ALLOC_ZONES
	struct m0_be_allocator_header *h;
	struct m0_be_alloc_zone       *z;
	m0_bcount_t                    full_size;

	h = a->ba_h;
#endif

	M0_PRE_EX(m0_be_allocator__invariant(a));
	M0_PRE(zonemask != 0);
	shift = max_check(shift, (unsigned) M0_BE_ALLOC_SHIFT_MIN);
	size = m0_align(size, 1UL << M0_BE_ALLOC_SHIFT_MIN);
#ifdef ENABLE_BE_ALLOC_ZONES
	full_size = size + sizeof(struct be_alloc_chunk);
#endif

	m0_be_op_active(op);

	m0_mutex_lock(&a->ba_lock);

#ifdef ENABLE_BE_ALLOC_ZONES
	if (m0_exists(i, ARRAY_SIZE(h->bah_zone),
		      ztype = i,
		      z = &h->bah_zone[i],
		      (zonemask & M0_BITS(i)) && z->baz_free >= full_size)) {
#endif
		/* algorithm starts here */
		m0_tl_for(chunks_free, bah_fl_s(a, size), iter) {
			++iter_nr;
			c = be_alloc_chunk_trysplit(a, tx, iter, size,
						    shift, ztype);
			if (c != NULL)
				break;
		} m0_tl_endfor;
		if (c == NULL) {
			m0_tl_for(chunks_free, bah_fl_s(a, 0), iter) {
				++iter_nr;
				c = be_alloc_chunk_trysplit(a, tx, iter, size,
							    shift, ztype);
				if (c != NULL)
					break;
			} m0_tl_endfor;
		}
		if (c != NULL && a->ba_trymerge)
			a->ba_trymerge = false;
		if (c == NULL) { /* last chance */
			int i;
			for (i = 0; i < BAH_FREE_NR; i++) {
				m0_tl_for(chunks_free, bah_fl_s(a, 0), iter) {
					++iter_nr;
					c = be_alloc_chunk_trysplit(a, tx, iter,
								    size, shift,
								    ztype);
					if (c != NULL)
						break;
				} m0_tl_endfor;
			}
			if (!a->ba_trymerge) {
				a->ba_trymerge = true;
				M0_LOG(M0_WARN, "trymerge is ON! Might be a "
				       "memleak or "
				       "wrong BE segment size configuration");
			}
		}
		if (c != NULL) {
			if (tx != NULL) {
				memset(&c->bac_mem, 0, c->bac_size);
				m0_be_tx_capture(tx, &M0_BE_REG(a->ba_seg,
								c->bac_size,
								&c->bac_mem));
			}
#ifdef ENABLE_BE_ALLOC_ZONES
			z->baz_free -= full_size;
			be_alloc_head_capture(a, tx);
#endif
		}
#ifdef ENABLE_BE_ALLOC_ZONES
	} else {
		M0_LOG(M0_WARN,
		       "Not enough free space, zone_mask=%"PRIu64" size=%"PRIu64
		       " nz_free=%"PRIu64" rz_free=%"PRIu64,
		       zonemask, size, h->bah_zone[M0_BAP_NORMAL].baz_free,
		       h->bah_zone[M0_BAP_REPAIR].baz_free);
	}
#endif
	op->bo_u.u_allocator.a_ptr = c == NULL ?    NULL : &c->bac_mem;
	op->bo_u.u_allocator.a_rc  = c == NULL ? -ENOMEM : 0;
	if (ptr != NULL)
		*ptr = op->bo_u.u_allocator.a_ptr;
	/* and ends here */
#ifdef ENABLE_BE_ALLOC_ZONES
	be_allocator_stats_update(&a->ba_h->bah_stats,
				  c == NULL ? size : c->bac_size, true, c == 0,
				  ztype);
#else
	be_allocator_stats_update(&a->ba_h->bah_stats,
				  c == NULL ? size : c->bac_size, true, c == 0,
				  0);
#endif
	M0_LOG(M0_DEBUG, "allocator=%p size=%lu shift=%u rc=%d iter_nr=%d "
	                 "trymerge=%x c=%p bac_size=%lu a_ptr=%p",
	       a, size, shift, op->bo_u.u_allocator.a_rc, iter_nr,
	       (unsigned)a->ba_trymerge,
	       c, c == NULL ? 0 : c->bac_size, op->bo_u.u_allocator.a_ptr);
	if (op->bo_u.u_allocator.a_rc != 0)
		be_allocator_stats_print(&a->ba_h->bah_stats);

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

	M0_POST_EX(m0_be_allocator__invariant(a));
	M0_POST(equi(op->bo_u.u_allocator.a_ptr != NULL,
		     op->bo_u.u_allocator.a_rc == 0));

	/* set op state after post-conditions because they are using op */
	m0_be_op_done(op);
}

M0_INTERNAL void m0_be_alloc(struct m0_be_allocator *a,
			     struct m0_be_tx *tx,
			     struct m0_be_op *op,
			     void **ptr,
			     m0_bcount_t size)
{
	m0_be_alloc_aligned(a, tx, op, ptr, size, M0_BE_ALLOC_SHIFT_MIN,
			    M0_BITS(M0_BAP_NORMAL));
}

M0_INTERNAL void m0_be_free_aligned(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op,
				    void *ptr)
{
	struct be_alloc_chunk *c;
	struct be_alloc_chunk *prev;
	struct be_alloc_chunk *next;
	bool		       merged;

	M0_PRE_EX(m0_be_allocator__invariant(a));
	M0_PRE(ergo(ptr != NULL, be_alloc_is_mem_in_allocator(a, 1, ptr)));

	m0_be_op_active(op);

	if (ptr != NULL) {
		m0_mutex_lock(&a->ba_lock);

		c = be_alloc_chunk_addr(ptr);
		M0_PRE(be_alloc_chunk_invariant(a, c));
		M0_PRE(!c->bac_free);
#ifdef ENABLE_BE_ALLOC_ZONES
		a->ba_h->bah_zone[c->bac_zone].baz_free += c->bac_size +
					 sizeof(struct be_alloc_chunk);
		be_allocator_stats_update(&a->ba_h->bah_stats, c->bac_size,
					  false, false, c->bac_zone);
		M0_LOG(M0_DEBUG, "allocator=%p c=%p c->bac_size=%lu zone=%d "
		       "data=%p", a, c, c->bac_size, c->bac_zone, &c->bac_mem);
#else
		be_allocator_stats_update(&a->ba_h->bah_stats, c->bac_size,
					  false, false, 0);
		M0_LOG(M0_DEBUG, "allocator=%p c=%p c->bac_size=%lu "
		       "data=%p", a, c, c->bac_size, &c->bac_mem);
#endif
		/* algorithm starts here */
		be_alloc_chunk_mark_free(a, tx, c);
		if (a->ba_trymerge) {
			prev = be_alloc_chunk_prev(a, c);
			next = be_alloc_chunk_next(a, c);
			merged = be_alloc_chunk_trymerge(a, tx, prev, c);
			if (merged)
				c = prev;
			be_alloc_chunk_trymerge(a, tx, c, next);
		}
#ifdef ENABLE_BE_ALLOC_ZONES
		be_alloc_head_capture(a, tx);
#endif
		/* and ends here */
		M0_POST(c->bac_free);
		M0_POST(c->bac_size > 0);
		M0_POST(be_alloc_chunk_invariant(a, c));

		m0_mutex_unlock(&a->ba_lock);
	}

	m0_be_op_done(op);

	M0_POST_EX(m0_be_allocator__invariant(a));
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
	M0_PRE_EX(m0_be_allocator__invariant(a));

	/** @todo GET_PTR a->ba_h */
	m0_mutex_lock(&a->ba_lock);
	*out = a->ba_h->bah_stats;
	m0_mutex_unlock(&a->ba_lock);
	/** @todo PUT_PTR a->ba_h */
}

M0_INTERNAL void m0_be_alloc_stats_credit(struct m0_be_allocator *a,
                                          struct m0_be_tx_credit *accum)
{
	m0_be_tx_credit_add(accum, &M0_BE_TX_CREDIT_PTR(&a->ba_h->bah_stats));
}

M0_INTERNAL void m0_be_alloc_stats_capture(struct m0_be_allocator *a,
                                           struct m0_be_tx        *tx)
{
	if (tx != NULL) {
		m0_be_tx_capture(tx, &M0_BE_REG_PTR(a->ba_seg,
						    &a->ba_h->bah_stats));
	}
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
