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
 */
int c2_bitmap_init(struct c2_bitmap *map, size_t nr);

/**
   Initialise a bitmap to hold nr bits with word array already
   pre-allocated. C2_BITMAP_WORDS() should be used to calculate the size of
   array.
 */
void c2_bitmap_init_inplace(struct c2_bitmap *map, size_t nr, uint64_t *bits);

/**
   Finalise the bitmap.
 */
void c2_bitmap_fini(struct c2_bitmap *map, bool inplace);

bool c2_bitmap_get(const struct c2_bitmap *map, size_t idx);
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
