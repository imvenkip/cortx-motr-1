#include "lib/c2bitops.h"
/* 31 ... 0 */
/**
 * find bit in range
 */
static int
bits_mask(long data, int start, int end)
{
	long mask = ~0UL << start;

	mask &= ~0UL >> (BITS_PER_LONG - end);
	return data & mask;
}

int c2_find_next_bit(long *string, uint32_t size, uint32_t start)
{
	uint32_t word = BIT_WORD(start);
	uint32_t last_word = BIT_WORD(size);
	uint32_t pos;
	uint32_t found;
	long mask;

	if (start >= size)
		return -1;

	/* part in start word */
	pos = start & (BITS_PER_LONG - 1);
	if (pos != 0) {
		mask = bits_mask(string[word], pos, BITS_PER_LONG);
		if (mask)
			goto found;
		word ++;
	}

	/* full words */
	while(word < last_word) {
		mask = string[word];
		if (mask)
			goto found;
		word++;
	}
	/* last (possible not full) word */
	pos = size & (BITS_PER_LONG - 1);
	if (pos != 0) {
		mask = bits_mask(string[word],0, pos);
		if (mask)
			goto found;
	}
	return -1;
found:
	return c2_ffs(mask) + word * BITS_PER_LONG;
}

static int
zero_mask(long data, int start, int end)
{
	long mask = ~0UL << start;

	mask &= ~0UL >> (BITS_PER_LONG - end);
	return data | ~mask;
}

int c2_find_next_zero_bit(long *string, uint32_t size, uint32_t start)
{
	uint32_t word = BIT_WORD(start);
	uint32_t last_word = BIT_WORD(size);
	uint32_t pos;
	uint32_t found;
	long mask;

	if (start >= size)
		return -1;

	/* part in start word */
	pos = start & (BITS_PER_LONG - 1);
	if (pos != 0) {
		mask = zero_mask(string[word], pos, BITS_PER_LONG);
		if (~mask)
			goto found;
		word ++;
	}

	/* full words */
	while(word < last_word) {
		mask = string[word];
		if (~mask)
			goto found;
		word++;
	}
	/* last (possible not full) word */
	pos = size & (BITS_PER_LONG - 1);
	if (pos != 0) {
		mask = zero_mask(string[word],0, pos);
		if (~mask)
			goto found;
	}
	return -1;
found:
	return c2_ffz(mask) + word * BITS_PER_LONG;

}
