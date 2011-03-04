/* -*- C -*- */

#include "lib/bitmap.h"
#include "lib/misc.h"   /* C2_SET0 */
#include "lib/arith.h"  /* C2_IS_PO2 */
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/memory.h"

/**
   @defgroup bitmap Bitmap
   @{
 */

/**
   Number of bits in a word (c2_bitmap.b_words).
   And the number of bits to shift to convert bit offset to word index.
 */
enum {
	C2_BITMAP_BITS = (8 * sizeof ((struct c2_bitmap *)0)->b_words[0]),
	C2_BITMAP_BITSHIFT = 6
};

C2_BASSERT(C2_IS_PO2(C2_BITMAP_BITS));
C2_BASSERT(C2_BITMAP_BITS == (1UL << C2_BITMAP_BITSHIFT));

/**
   Number of words needed to be allocated to store nr bits.  This is
   an allocation macro.  For indexing, C2_BITMAP_SHIFT is used.

   @param nr number of bits to be allocated
 */
#define C2_BITMAP_WORDS(nr) (((nr) + (C2_BITMAP_BITS-1)) >> C2_BITMAP_BITSHIFT)

/* verify the bounds of size macro */
C2_BASSERT(C2_BITMAP_WORDS(0) == 0);
C2_BASSERT(C2_BITMAP_WORDS(1) == 1);
C2_BASSERT(C2_BITMAP_WORDS(63) == 1);
C2_BASSERT(C2_BITMAP_WORDS(64) == 1);
C2_BASSERT(C2_BITMAP_WORDS(65) == 2);

/**
   Shift a c2_bitmap bit index to get the word index.
   Use C2_BITMAP_SHIFT() to select the correct word, then use C2_BITMAP_MASK()
   to access the individual bit within that word.

   @param idx bit offset into the bitmap
 */
#define C2_BITMAP_SHIFT(idx) ((idx) >> C2_BITMAP_BITSHIFT)

/**
   Mask off a single bit within a word.
   Use C2_BITMAP_SHIFT() to select the correct word, then use C2_BITMAP_MASK()
   to access the individual bit within that word.

   @param idx bit offset into the bitmap
 */
#define C2_BITMAP_MASK(idx) (1UL << ((idx) & (C2_BITMAP_BITS-1)))

int c2_bitmap_init(struct c2_bitmap *map, size_t nr)
{
	int ret = 0;

	map->b_nr = nr;
	C2_ALLOC_ARR(map->b_words, C2_BITMAP_WORDS(nr));
	if (map->b_words == NULL)
		ret = -ENOMEM;
	return ret;
}
C2_EXPORTED(c2_bitmap_init);

void c2_bitmap_fini(struct c2_bitmap *map)
{
	C2_ASSERT(map->b_words != NULL);
	c2_free(map->b_words);
	C2_SET0(map);
}
C2_EXPORTED(c2_bitmap_fini);

bool c2_bitmap_get(const struct c2_bitmap *map, size_t idx)
{
	bool result = false;

	C2_PRE(idx < map->b_nr);
	result = map->b_words[C2_BITMAP_SHIFT(idx)] & C2_BITMAP_MASK(idx);
	return result;
}
C2_EXPORTED(c2_bitmap_get);

void c2_bitmap_set(struct c2_bitmap *map, size_t idx, bool val)
{
	C2_ASSERT(idx < map->b_nr);
	if (val)
		map->b_words[C2_BITMAP_SHIFT(idx)] |= C2_BITMAP_MASK(idx);
	else
		map->b_words[C2_BITMAP_SHIFT(idx)] &= ~C2_BITMAP_MASK(idx);
}
C2_EXPORTED(c2_bitmap_set);

/** @} end of bitmap group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
