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
 * Original author: Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 02/18/2011
 */

#pragma once

#ifndef __COLIBRI_LIB_BITMAP_H__
#define __COLIBRI_LIB_BITMAP_H__

#include "lib/types.h"
#include "lib/assert.h"

/**
   @defgroup bitmap Bitmap
   @{
 */

/**
   An array of bits (Booleans).

   The bitmap is stored as an array of 64-bit "words"
 */
struct c2_bitmap {
	/** Number of bits in this map. */
	size_t    b_nr;
	/** Words with bits. */
	uint64_t *b_words;
};

/**
   Initialise a bitmap to hold nr bits. The array to store bits is
   allocated internally.

   On success, the bitmap is initialised with all bits initially
   set to false.

   @param map bitmap object to initialize
   @param nr  size of the bitmap, in bits
   @retval 0 success
   @retval !0 failure, -errno
 */
C2_INTERNAL int c2_bitmap_init(struct c2_bitmap *map, size_t nr);

/**
   Finalise the bitmap.
   All memory associated with the bitmap is released.

   @param map bitmap to finalise
 */
C2_INTERNAL void c2_bitmap_fini(struct c2_bitmap *map);

/**
   Get a bit value from a bitmap.

   @pre idx < map->b_br

   @param map bitmap to query
   @param idx bit offset in the bitmap to query
   @return the bit value, true or false.
 */
C2_INTERNAL bool c2_bitmap_get(const struct c2_bitmap *map, size_t idx);

/**
   Set a bit value in a bitmap.

   @param map bitmap to modify
   @param idx bit offset to modify.  Attempting to set a bit beyond the size
   of the bitmap results is not allowed (causes and assert to fail).
   @param val new bit value, true or false
 */
C2_INTERNAL void c2_bitmap_set(struct c2_bitmap *map, size_t idx, bool val);

/**
   Copies the bit values from one bitmap to another.
   @param dst destination bitmap, must already be initialised.  If dst
   is larger than src, bits beyond src->b_nr are cleared in dst.
   @param src source bitmap
   @pre dst->b_nr >= src->b_nr
 */
C2_INTERNAL void c2_bitmap_copy(struct c2_bitmap *dst,
				const struct c2_bitmap *src);

C2_BASSERT(8 == sizeof ((struct c2_bitmap *)0)->b_words[0]);

/** @} end of bitmap group */

/* __COLIBRI_LIB_BITMAP_H__ */
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
