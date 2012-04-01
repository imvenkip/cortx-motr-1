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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 08/13/2010
 */

#ifndef __COLIBRI_DB_EXTMAP_H__
#define __COLIBRI_DB_EXTMAP_H__

/**
   @defgroup extmap Extent map abstraction

   Extent map is a persistent transactional collection of extents in an abstract
   numerical name-space with a numerical value associated to each extent.

   The name-space is a set of all 64-bit unsigned numbers from 0 to
   C2_BINDEX_MAX. An extent of the name-space consisting of numbers from A
   (inclusive) to B (exclusive) is denoted [A, B).

   A segment is an extent together with a 64-bit value associated with it,
   denoted as ([A, B), V).

   An extent map is a collection of segments whose extents are non-empty and
   form a partition of the name-space.

   That is, an extent map is something like

   @f[
           ([0, e_0), v_0), ([e_0, e_1), v_1), \ldots,
	   ([e_n, C2\_BINDEX\_MAX + 1), v_n)
   @f]

   Note that extents cover the whole name-space from 0 to C2_BINDEX_MAX without
   holes.

   Possible applications of extent map include:

     - allocation data for a data object. In this case an extent in the
       name-space is interpreted as an extent in the logical offset space of
       data object. A value associated with the extent is a starting block of a
       physical extent allocated to the logical extent. In addition to allocated
       extents, a map might contain "holes" and "not-allocated" extents, tagged
       with special otherwise impossible values;

     - various resource identifier distribution maps: file identifiers,
       container identifiers, layout identifiers, recording state of resource
       name-spaces: allocated to a certain node, free, etc.

   Extent map interface is based on a notion of map cursor (c2_emap_cursor): an
   object recording a position within a map (i.e., a segment reached by the
   iteration).

   A cursor can be positioned at the segment including a given point in the
   name-space (c2_emap_lookup()) and moved through the segments (c2_emap_next()
   and c2_emap_prev()).

   An extent map can be modified by the following functions:

     - c2_emap_split(): split a segment into a collection of segments with given
       lengths and values, provided that their total length is the same as the
       length of the original segment;

     - c2_emap_merge(): merge part of a segment into the next segment. The
       current segment is shrunk (or deleted if it would become empty) and the
       next segment is expanded downward;

     - c2_emap_paste() handles more complicated cases.

   It's easy to see that these operations preserve extent map invariant that
   extents are non-empty and form the name-space partition.

   @note The asymmetry between split and merge interfaces (i.e., the fact that a
   segment can be split into multiple segments at once, but only two segments
   can be merged) is because a user usually wants to inspect a segment before
   merging it with another one. For example, data object truncate goes through
   the allocation data segments downward until the target offset of
   reached. Each segment is analyzed, data-blocks are freed is necessary and the
   segment is merged with the next one.

   @note Currently the length and ordering of prefix and value is fixed by the
   implementation. Should the need arise, prefixes and values of arbitrary size
   and ordering could be easily implemented at the expense of dynamic memory
   allocation during cursor initialization. Prefix comparison function could be
   supplied as c2_emap constructor parameter.

   @{
 */

#include "lib/ext.h"       /* c2_ext */
#include "lib/types.h"     /* struct c2_uint128 */
#include "db/db.h"

/* import */
struct c2_emap;
struct c2_dbenv;
struct c2_db_tx;
struct c2_indexvec;

/* export */
struct c2_emap_seg;
struct c2_emap_cursor;

/**
    Create maps collection.

    @param db - data-base environment used for persistency and transactional
    support.
 */
int  c2_emap_init(struct c2_emap *emap,
		  struct c2_dbenv *db, const char *mapname);

/** Release the resources associated with the collection. */
void c2_emap_fini(struct c2_emap *emap);

/**
   Insert a new map with the given prefix into the collection.

   Initially new map consists of a single extent:

   @f[
	   ([0, C2\_BINDEX\_MAX + 1), val)
   @f]
 */
int c2_emap_obj_insert(struct c2_emap *emap, struct c2_db_tx *tx,
		       const struct c2_uint128 *prefix, uint64_t val);

/**
   Remove a map with the given prefix from the collection.

   @pre the map must be in initial state: consists of a single extent, covering
   the whole name-space.
 */
int c2_emap_obj_delete(struct c2_emap *emap, struct c2_db_tx *tx,
		       const struct c2_uint128 *prefix);

/** Extent map segment. */
struct c2_emap_seg {
	/** Map prefix, identifying the map in its collection. */
	struct c2_uint128 ee_pre;
	/** Name-space extent. */
	struct c2_ext     ee_ext;
	/** Value associated with the extent. */
	uint64_t          ee_val;
};

/** True iff the extent is the last one in a map. */
bool c2_emap_ext_is_last(const struct c2_ext *ext);

/** True iff the extent is the first one in a map. */
bool c2_emap_ext_is_first(const struct c2_ext *ext);

/** Returns an extent at the current cursor position. */
struct c2_emap_seg *c2_emap_seg_get(struct c2_emap_cursor *iterator);

/**
    Initialises extent map cursor to point to the segment containing given
    offset in a map with a given prefix in a given collection.

    All operations done through this cursor are executed in the context of given
    transaction.

    @pre offset <= C2_BINDEX_MAX

    @retval -ESRCH no matching segment is found. The cursor is non-functional,
    but c2_emap_seg_get() contains information about the first segment of the
    next map (in prefix lexicographical order);

    @retval -ENOENT no matching segment is found and there is no map following
    requested one.
 */
int c2_emap_lookup(struct c2_emap *emap, struct c2_db_tx *tx,
		   const struct c2_uint128 *prefix, c2_bindex_t offset,
		   struct c2_emap_cursor *it);

/**
   Move cursor to the next segment in its map.

   @pre !c2_emap_ext_is_last(c2_emap_seg_get(iterator))
 */
int c2_emap_next(struct c2_emap_cursor *iterator);

/**
   Move cursor to the previous segment in its map.

   @pre !c2_emap_ext_is_first(c2_emap_seg_get(iterator))
 */
int c2_emap_prev(struct c2_emap_cursor *iterator);

/**
   Split the segment the cursor is current positioned at into a collection of
   segments given by the vector.

   @param vec - a vector describing the collection of
   segments. vec->ov_vec.v_count[] array contains lengths of the extents and
   vec->ov_index[] array contains values associated with the corresponding
   extents.

   Empty segments from vec are skipped.  On successful completion, the cursor is
   positioned on the last created segment.

   @pre c2_vec_count(&vec->ov_vec) == c2_ext_length(c2_emap_seg_get(iterator))
 */
int c2_emap_split(struct c2_emap_cursor *iterator, struct c2_indexvec *vec);

/**
   Paste segment (ext, val) into the map, deleting or truncating overlapping
   segments as necessary.

   @param del - this call-back is called when an existing segment is completely
   covered by a new one and has to be deleted. The segment to be deleted is
   supplied as the call-back argument;

   @param cut_left - this call-back is called when an existing segment has to be
   cut to give place to a new one and some non-empty left part of the existing
   segment remains in the map. c2_ext call-back argument is the extent being cut
   from the existing segment. The last argument is the value associated with the
   existing segment. The call-back must set seg->ee_val to the new value
   associated with the remaining left part of the call-back;

   @param cut_right - similar to cut_left, this call-back is called when some
   non-empty part of an existing segment survives the paste operation.

   @note It is possible that both left and right cut call-backs are called
   against the same segment (in the case where new segment fits completely into
   existing one).

   @note Map invariant is temporarily violated during paste operation. No calls
   against the map should be made from the call-backs or, more generally, from
   the same transaction, while paste is running.

   @note Call-backs are called in the order of cursor iteration, but this is not
   a part of official function contract.
 */
int c2_emap_paste(struct c2_emap_cursor *it, struct c2_ext *ext, uint64_t val,
		  void (*del)(struct c2_emap_seg *),
		  void (*cut_left)(struct c2_emap_seg *, struct c2_ext *,
				   uint64_t),
		  void (*cut_right)(struct c2_emap_seg *, struct c2_ext *,
				    uint64_t));

/**
   Merge a part of the segment the cursor is currently positioned at with the
   next segment in the map.

   Current segment's extent is shrunk by delta. If this would make it empty, the
   current segment is deleted. The next segment is expanded by delta downwards.

   @pre !c2_emap_ext_is_last(c2_emap_seg_get(iterator))
   @pre delta <= c2_ext_length(c2_emap_seg_get(iterator));
 */
int c2_emap_merge(struct c2_emap_cursor *iterator, c2_bindex_t delta);

/**
   Release the resources associated with the cursor.
 */
void c2_emap_close(struct c2_emap_cursor *iterator);

#include "db/extmap_internal.h"

/**
    Extent map caret.

    A caret is an iterator with finer granularity than a cursor. A caret is a
    cursor plus an offset within the segment the cursor is currently over.

    Caret interface is intentionally similar to c2_vec_cursor interface, which
    see for further references.

    Caret implementation is simplified by segment non-emptiness (as guaranteed
    by extent map invariant).
 */
struct c2_emap_caret {
	struct c2_emap_cursor *ct_it;
	c2_bindex_t            ct_index;
};

void c2_emap_caret_init(struct c2_emap_caret *car,
			struct c2_emap_cursor *it, c2_bindex_t index);
void c2_emap_caret_fini(struct c2_emap_caret *car);
int  c2_emap_caret_move(struct c2_emap_caret *car, c2_bcount_t count);

c2_bcount_t c2_emap_caret_step(const struct c2_emap_caret *car);

/** @} end group extmap */

/* __COLIBRI_DB_EXTMAP_H__ */
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
