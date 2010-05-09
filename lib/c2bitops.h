/* -*- C -*- */
#ifndef __COLIBRI_LIB_BITOPS_H_

#define __COLIBRI_LIB_BITOPS_H_

#include "cdefs.h

#define BIT_WORD(n)	((n) / BITS_PER_LONG)

/**
 * set bit in bitstring
 *
 * @param val pointer to bit string
 * @param num bit number
 *
 * @return none
 */
static inline void
c2_bit_set(unsigned long *val, unsigned int num)
{
	int pos = BIT_WORD(num);
	int bit = num & (BITS_PER_LONG - 1);

	val[pos] |= 1 << bit;
}

/**
 * clear bit in bitstring
 *
 * @param val pointer to bit string
 * @param num bit number
 *
 * @return none
 */
static inline void
c2_bit_clear(unsigned long *val, unsigned int num)
{
	int pos = BIT_WORD(num);
	int bit = num & (BITS_PER_LONG - 1);

	val[pos] &= ~(1 << bit);
}

/**
 * check bit is set in bitstring
 *
 * @param val pointer to bit string
 * @param num bit number
 *
 * @retval true if bit set
 * @retval false if bit not set
 */
static inline bool
c2_bit_is_set(unsigned long *val, int num)
{
	int pos = BIT_WORD(num);
	int bit = num & (BITS_PER_LONG - 1);

	return !!(val[pos] & (1 << bit));
}


/**
 * find last (most-significant) bit set, using binary search
 *
 * @param data - bit mask
 *
 * @return bit number (0 .. BITS_PER_LONG-1), or -1 if none bits set
 */
static inline int 
c2_fls(unsigned long data)
{
	int pos = BITS_PER_LONG - 1;

	if (data == 0)
		return -1;

	if (!(data & (~0ul << (BITS_PER_LONG - BITS_PER_LONG / 2)))) {
		data <<= (BITS_PER_LONG / 2);
		pos -= (BITS_PER_LONG / 2);
	}

	if (!(data & (~0ul << (BITS_PER_LONG - BITS_PER_LONG  / 4)))) {
		data <<= (BITS_PER_LONG / 4);
		pos -= (BITS_PER_LONG / 4);
	}

	if (!(data & (~0ul << (BITS_PER_LONG - BITS_PER_LONG / 8)))) {
		data <<= (BITS_PER_LONG / 8);
		pos -= (BITS_PER_LONG / 8);
	}

	if (!(data & (~0ul << (BITS_PER_LONG - BITS_PER_LONG / 16)))) {
		data <<= (BITS_PER_LONG / 16);
		pos -= (BITS_PER_LONG / 16);
	}

	if (!(data & (~0ul << (BITS_PER_LONG - BITS_PER_LONG / 32)))) {
		data <<= (BITS_PER_LONG / 32);
		pos -= (BITS_PER_LONG / 32);
	}

#if BITS_PER_LONG == 64
	if (!(data & (~0ul << (BITS_PER_LONG - BITS_PER_LONG / 64)))) {
		data <<= (BITS_PER_LONG / 64);
		pos -= (BITS_PER_LONG / 64);
	}
#endif
	return pos;
}

/**
 * find first (less-significant) bit set, using binary search
 *
 * @param data - bit mask
 *
 * @return bit number (0 .. BITS_PER_LONG -1), or -1 if none bits set.
 */
static inline int 
c2_ffs(unsigned long data)
{
	int pos = 0;

	if (data == 0)
		return -1;

	if (!(data & (~0ul >> (BITS_PER_LONG - BITS_PER_LONG / 2)))) {
		data >>= (BITS_PER_LONG / 2);
		pos += (BITS_PER_LONG / 2);
	}

	if (!(data & (~0ul >> (BITS_PER_LONG - BITS_PER_LONG / 4)))) {
		data >>= (BITS_PER_LONG / 4);
		pos += (BITS_PER_LONG / 4);
	}

	if (!(data & (~0ul >> (BITS_PER_LONG - BITS_PER_LONG / 8)))) {
		data >>= (BITS_PER_LONG / 8);
		pos += (BITS_PER_LONG / 8);
	}

	if (!(data & (~0ul >> (BITS_PER_LONG - BITS_PER_LONG / 16)))) {
		data >>= (BITS_PER_LONG / 16);
		pos += (BITS_PER_LONG / 16);
	}

	if (!(data & (~0ul >> (BITS_PER_LONG - BITS_PER_LONG / 32)))) {
		data >>= (BITS_PER_LONG / 32);
		pos += (BITS_PER_LONG / 32);
	}

#if BITS_PER_LONG == 64
	if (!(data & (~0ul >> (BITS_PER_LONG - BITS_PER_LONG / 64)))) {
		data >>= (BITS_PER_LONG / 64);
		pos += (BITS_PER_LONG / 64);
	}
#endif

	return pos;
}

#define c2_ffz(w)	c2_ffs(~(w))

/**
 * find first non zero bit after position
 *
 * @param string pointer to bitstring
 * @param size total size in bits
 * @param start start position to search
 *
 * retval -1 is none non zero bits found after start position
 * retval >0 position of non zero bit
 */
int c2_find_next_bit(long *string, uint32_t size, uint32_t start);

/**
 * find first zero bit after position
 *
 * @param string pointer to bitstring
 * @param size total size in bits
 * @param start start position to search
 *
 * retval -1 is none non zero bits found after start position
 * retval >0 position of non zero bit
 */

int c2_find_next_zero_bit(long *string, uint32_t size, uint32_t start);

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
