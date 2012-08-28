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
void *c2_bitstring_buf_get(struct c2_bitstring *c);
/**
 Report the bitstring length
 */
uint32_t c2_bitstring_len_get(const struct c2_bitstring *c);
/**
 Set the bitstring valid length
 */
void c2_bitstring_len_set(struct c2_bitstring *c, uint32_t len);
/**
 String-like compare: alphanumeric for the length of the shortest string.
 Shorter strings are "less" than matching longer strings.
 Bitstrings may contain embedded NULLs.
 */
int c2_bitstring_cmp(const struct c2_bitstring *c1,
                     const struct c2_bitstring *c2);


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
