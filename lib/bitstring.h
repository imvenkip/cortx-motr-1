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
 * Original author: Nathan Rutman <Nathan_Rutman@xyratex.com>
 * Original creation date: 11/17/2010
 */

#pragma once

#ifndef __COLIBRI_LIB_BITSTRING_H__
#define __COLIBRI_LIB_BITSTRING_H__

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

/**
  Get a pointer to the data in the bitstring.
  Data may be read or written here.

  User is responsible for allocating large enough contiguous memory.
 */
C2_INTERNAL void *c2_bitstring_buf_get(struct c2_bitstring *c);
/**
 Report the bitstring length
 */
C2_INTERNAL uint32_t c2_bitstring_len_get(const struct c2_bitstring *c);
/**
 Set the bitstring valid length
 */
C2_INTERNAL void c2_bitstring_len_set(struct c2_bitstring *c, uint32_t len);
/**
 String-like compare: alphanumeric for the length of the shortest string.
 Shorter strings are "less" than matching longer strings.
 Bitstrings may contain embedded NULLs.
 */
C2_INTERNAL int c2_bitstring_cmp(const struct c2_bitstring *c1,
				 const struct c2_bitstring *c2);

/**
 Copy @src to @dst.
*/
C2_INTERNAL void c2_bitstring_copy(struct c2_bitstring *dst,
				   const char *src, size_t count);

/**
 Alloc memory for a string of passed len and copy name to it.
*/
C2_INTERNAL struct c2_bitstring *c2_bitstring_alloc(const char *name,
						    size_t len);

/**
 Free memory of passed @c.
*/
C2_INTERNAL void c2_bitstring_free(struct c2_bitstring *c);

/** @} end of adt group */


/* __COLIBRI_LIB_BITSTRING_H__ */
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
