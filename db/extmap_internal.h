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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 08/13/2010
 */

#pragma once

#ifndef __COLIBRI_DB_EXTMAP_INTERNAL_H__
#define __COLIBRI_DB_EXTMAP_INTERNAL_H__

/**
   @addtogroup extmap

   <b>Extent map implementation.</b>

   Extent map collection (c2_emap) is a table. 128-bit prefix is used to store
   multiple extent maps in the same table.

   @{
 */

#include "lib/types.h"     /* struct c2_uint128 */
#include "db/db.h"

/**
   c2_emap stores a collection of related extent maps. Individual maps within a
   collection are identified by a prefix.

   @see c2_emap_obj_insert()
 */
struct c2_emap {
	struct c2_table   em_mapping;
};

/**
   A key used to identify a particular segment in the map collection.
 */
struct c2_emap_key {
	/** Prefix of the map the segment is part of. */
	struct c2_uint128 ek_prefix;
	/** Last offset of the segment's extent. That is, the key of a segment
	    ([A, B), V) has B as an offset.

	    This not entirely intuitive decision is forced by the available
	    range search interfaces of c2_db_cursor: c2_db_cursor_get()
	    positions the cursor on the least key not less than the key sought
	    for.
	 */
	c2_bindex_t       ek_offset;
};

/**
   A record stored in the table for each segment in the map collection.

   @note Note that there is a certain amount of redundancy: for any but the
   first segment in the map, its starting offset is equal to the last offset of
   the previous segment and for the first segment, the starting offset is
   0. Consequently, c2_emap_rec::er_start field can be eliminated reducing
   storage foot-print at the expense of increase in code complexity and
   possibility of occasional extra IO.
 */
struct c2_emap_rec {
	/**
	   Starting offset of the segment's extent.
	 */
	c2_bindex_t   er_start;
	/**
	   Value associated with the segment.
	 */
	uint64_t      er_value;
};

/**
   Cursor iterating through the extent map.
 */
struct c2_emap_cursor {
	/** Map this cursor is iterating through. */
	struct c2_emap     *ec_map;
	/** Segment currently reached. */
	struct c2_emap_seg  ec_seg;
	/** Data-base cursor. */
	struct c2_db_cursor ec_cursor;
	struct c2_db_pair   ec_pair;
	struct c2_emap_key  ec_key;
	struct c2_emap_rec  ec_rec;
	struct c2_uint128   ec_prefix;
};

/** @} end group extmap */

/* __COLIBRI_DB_EXTMAP_INTERNAL_H__ */
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
