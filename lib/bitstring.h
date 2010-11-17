/* -*- C -*- */

#ifndef __COLIBRI_BITSTRING_H__
#define __COLIBRI_BITSTRING_H__

#include "lib/types.h"
#include "lib/cdefs.h"

/**
   @addtogroup adt Basic abstract data types
   @{
*/

struct c2_bitstring {
	uint32_t b_len;
	char     b_data[0];
};

void *c2_bitstring_buf_get(struct c2_bitstring *c);
uint32_t c2_bitstring_len_get(struct c2_bitstring *c);
void c2_bitstring_len_set(struct c2_bitstring *c, uint32_t len);
int c2_bitstring_cmp(const struct c2_bitstring *c1,
                     const struct c2_bitstring *c2);


/** @} end of adt group */


/* __COLIBRI_BITSTRING_H__ */
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
