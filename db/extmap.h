/* -*- C -*- */

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
	   ([e_n, C2_BINDEX_MAX + 1), v_n)
   @f]

   Note that extents cover the whole name-space from 0 to C2_BINDEX_MAX without
   holes.

   Possible applications of extent map include:

   @li allocation data for a data object. In this case an extent in the
   name-space is interpreted as an extent in the logical offset space of data
   object. A value associated with the extent is a starting block of a physical
   extent allocated to the logical extent. In addition to allocated extents, a
   map might contain "holes" and "not-allocated" extents, tagged with special
   otherwise impossible values;

   @li various resource identifier distribution maps: file identifiers,
   container identifiers, layout identifiers, recording state of resource
   name-spaces: allocated to a certain node, free, etc.

   Extent map interface is based on a notion of map cursor (c2_emap_cursor): an
   object recording a position within a map (i.e., a segment reached by the
   iteration).

   A cursor can be positioned at the segment including a given point in the
   name-space (c2_emap_lookup()) and moved through the segments (c2_emap_next()
   and c2_emap_prev()).

   An extent map can be modified by two functions:

   @li c2_emap_split(): split a segment into a collection of segments with given
   lengths and values, provided that their total length is the same as the
   length of the original segment;

   @li c2_emap_merge(): merge part of a segment into the next segment. The
   current segment is shrunk (or deleted if it would become empty) and the next
   segment is expanded downward.

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
	   ([0, C2_BINDEX_MAX + 1), val)
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
