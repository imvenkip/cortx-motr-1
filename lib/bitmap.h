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

#ifndef __MERO_LIB_BITMAP_H__
#define __MERO_LIB_BITMAP_H__

#include "lib/types.h"
#include "lib/assert.h"
#include "xcode/xcode_attr.h"

/**
   @defgroup bitmap Bitmap
   @{
 */

/**
   An array of bits (Booleans).

   The bitmap is stored as an array of 64-bit "words"
 */
struct m0_bitmap {
	/** Number of bits in this map. */
	size_t    b_nr;
	/** Words with bits. */
	uint64_t *b_words;
};

struct m0_bitmap_onwire {
	/** size of bo_words. */
	size_t    bo_size;
	/** Words with bits. */
	uint64_t *bo_words;
} M0_XCA_SEQUENCE;

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
M0_INTERNAL int m0_bitmap_init(struct m0_bitmap *map, size_t nr);

/**
   Finalise the bitmap.
   All memory associated with the bitmap is released.

   @param map bitmap to finalise
 */
M0_INTERNAL void m0_bitmap_fini(struct m0_bitmap *map);

/**
   Get a bit value from a bitmap.

   @pre idx < map->b_br

   @param map bitmap to query
   @param idx bit offset in the bitmap to query
   @return the bit value, true or false.
 */
M0_INTERNAL bool m0_bitmap_get(const struct m0_bitmap *map, size_t idx);

/**
   Set a bit value in a bitmap.

   @param map bitmap to modify
   @param idx bit offset to modify.  Attempting to set a bit beyond the size
   of the bitmap results is not allowed (causes and assert to fail).
   @param val new bit value, true or false
 */
M0_INTERNAL void m0_bitmap_set(struct m0_bitmap *map, size_t idx, bool val);

/**
   Copies the bit values from one bitmap to another.
   @param dst destination bitmap, must already be initialised.  If dst
   is larger than src, bits beyond src->b_nr are cleared in dst.
   @param src source bitmap
   @pre dst->b_nr >= src->b_nr
 */
M0_INTERNAL void m0_bitmap_copy(struct m0_bitmap *dst,
				const struct m0_bitmap *src);

/**
 * Returns the number of bits that are 'true'.
 */
M0_INTERNAL size_t m0_bitmap_set_nr(const struct m0_bitmap *map);

/**
   Initialise an onwire bitmap to hold nr bits. The array to store bits is
   allocated internally.

   On success, the bitmap is initialised with all bits initially set to false.

   @param ow_map onwire bitmap object to initialize
   @param nr  size of the bitmap, in bits
   @retval 0 success
   @retval !0 failure, -errno
 */
M0_INTERNAL int m0_bitmap_onwire_init(struct m0_bitmap_onwire *ow_map,
				      size_t nr);
/**
   Finalise the onwire bitmap.
   All memory associated with the onwire bitmap is released.

   @param map bitmap to finalise
 */
M0_INTERNAL void m0_bitmap_onwire_fini(struct m0_bitmap_onwire *ow_map);

/**
   Converts in mermory struct m0_bitmap to onwire struct m0_bitmap_onwire.

   @param im_map in-memory bitmap
   @param ow_map pre-intialised onwire bitmap object with conversion result
		 from in-memory bitmap.
 */
M0_INTERNAL void m0_bitmap_im2ow(const struct m0_bitmap *im_map,
			         struct m0_bitmap_onwire *ow_map);

/**
   Converts onwire bitmap to in-memory bitmap.

   @param ow_map onwire bitmap object to be converted into in-memory bitmap.
   @param im_map pre-intialised in-memory bitmap object with conversion result
	      from onwire bitmap.
 */
M0_INTERNAL void m0_bitmap_ow2im(const struct m0_bitmap_onwire *ow_map,
				 struct m0_bitmap *im_map);

M0_BASSERT(8 == sizeof ((struct m0_bitmap *)0)->b_words[0]);

/** @} end of bitmap group */
#endif /* __MERO_LIB_BITMAP_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
