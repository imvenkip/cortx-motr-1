/* -*- C -*- */

#ifndef __COLIBRI_LIB_BITMAP_H__
#define __COLIBRI_LIB_BITMAP_H__

#include "lib/types.h"

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

   @param map bitmap object to initialize
   @param nr  size of the bitmap, in bits
   @retval 0 success
   @retval !0 failure, -errno
 */
int c2_bitmap_init(struct c2_bitmap *map, size_t nr);

/**
   Initialise a bitmap to hold nr bits with word array already
   pre-allocated. C2_BITMAP_WORDS() should be used to calculate the size of
   array.  Note that the caller must track that the bitmap was
   pre-allocated.

   @see c2_bitmap_fini

   @param map bitmap object to initialize
   @param nr size of the bitmap in bits
   @param bits pre-allocated memory to apply to the bitmap
 */
void c2_bitmap_init_inplace(struct c2_bitmap *map, size_t nr, uint64_t *bits);

/**
   Finalise the bitmap.

   In the case that the bitmap memory was pre-allocated, finalisation will
   not free the memory.

   @see c2_bitmap_init_inplace

   @param map bitmap to finalise
   @param inplace finalise a pre-allocated bitmap.
 */
void c2_bitmap_fini(struct c2_bitmap *map, bool inplace);

/**
   Get a bit value from a bitmap.

   @param map bitmap to query
   @param idx bit offset in the bitmap to query
   @return the bit value, true or false.  Querying for a bit beyond the size
   of the bitmap always returns false.
 */

bool c2_bitmap_get(const struct c2_bitmap *map, size_t idx);
/**
   Set a bit value in a bitmap.

   @param map bitmap to modify
   @param idx bit offset to modify.  Attempting to set a bit beyond the size
   of the bitmap results is not allowed (causes and assert to fail).
   @param val new bit value, true or false
 */
void c2_bitmap_set(struct c2_bitmap *map, size_t idx, bool val);

C2_BASSERT(8 == sizeof ((struct c2_bitmap *)0)->b_words[0]);

#define C2_BITMAP_WORDS(nr) (((nr) + 7) >> 3)

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
