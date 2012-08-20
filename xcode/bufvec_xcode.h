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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 06/25/2011
 */

#pragma once

#ifndef __COLIBRI_BUFVEC_XCODE_H__
#define __COLIBRI_BUFVEC_XCODE_H__

/**
   @defgroup bufvec_xcode Generic Buffer Vector Encode/Decode routines.

   Encode/Decode functions for encoding/decoding various atomic types into
   buffer vectors. The functionality is very similar to the XDR functions.
   These routines can be used to implement a type encode/decode routine for
   each FOP, ADDB records etc.

   XXX:
   - We don't care about endianness currently. However, for this to work
   across different platforms and architectures, correct endianness conversion
   needs to be implemented. This will be taken up in the future.
   - Currently, we assume that the bufvecs supplied to the transcode routines
   have 8 byte aligned buffers with sizes multiple of 8 bytes.
*/

#include "lib/vec.h"
#include "fop/fop.h"

/** This is the  number of bytes per unit of external data */
enum {
	MAX_PAD_BYTES = 7,
	BYTES_PER_XCODE_UNIT = 8,
	XCODE_UNIT_ALIGNED_MASK = ~(MAX_PAD_BYTES)
};

/**
  These are the operations that can be performed by the xcode routines on the
  bufvecs. BUFVEC_ENCODE causes the type to be encoded into the bufvec.
  BUFVEC_DECODE causes the type to be extracted from the bufvec.
*/
enum c2_bufvec_what {
	C2_BUFVEC_ENCODE = 0,
	C2_BUFVEC_DECODE = 1,
};

/** Enums used by generic fop encode-decode interfaces */
enum {
	FOP_FIELD_ZERO = 0,
	ELEMENT_ZERO   = 0,
	FOP_FIELD_ONE  = 1,
};

/**
  Encode/Decode the various C builtin atomic types into c2_bufvecs. Each of
  these routines provide a single procedure for both encode and decode for each
  data type. This helps to keep the encode and decode procedures for a data
  type consistent.

  @param vc Current bufvec cursor position. For 8 byte aligned buffers, which
  is the case currently, the address at this cursor position would be 8-byte
  aligned.
  @param val Value which is to be encoded/decoded from the bufvec. Currently,
  fix width datatypes are supported(uint8_t, uint16_t, uint32_t, uint64_t).
  @param what The type of operation that is to be performed on the data -
   encode OR decode.
  @retval 0 On Success.
  @retval -errno On Failure.
*/

int c2_bufvec_uint64(struct c2_bufvec_cursor *vc, uint64_t *val,
		     enum c2_bufvec_what what);

/**
  Encode/Decode a 32 bit unsigned value into c2_bufvecs.
  @see c2_bufvec_uint64()
*/
int c2_bufvec_uint32(struct c2_bufvec_cursor *vc, uint32_t *val,
		     enum c2_bufvec_what what);
/**
  Encode/Decode a 16 bit unsigned value into c2_bufvecs.
  @see c2_bufvec_uint64()
*/
int c2_bufvec_uint16(struct c2_bufvec_cursor *vc, uint16_t *val,
		     enum c2_bufvec_what what);

/** @} end of bufvec group */

#endif /* __COLIBRI_BUFVEC_XCODE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
