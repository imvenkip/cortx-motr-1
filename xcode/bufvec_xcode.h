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

#include "lib/vec.h"
#include "fop/fop.h"

#ifndef C2_BUFVEC_XCODE_H_
#define C2_BUFVEC_XCODE_H_
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

/**
  Encode/Decode a 8  bit unsigned value into c2_bufvecs.
  @see c2_bufvec_uint64()
*/
int c2_bufvec_byte(struct c2_bufvec_cursor *vc, uint8_t *val,
		     enum c2_bufvec_what what);

/**
 Generic procedure for each data type for encode / decode. This can be used
 for creating bufvec encode/decode for user defined data types. This can also
 be passed as a callback function for xcoding each element when xcoding
 an array (see c2_bufvec_array).
 */
typedef int (*c2_bufvec_xcode_t)(struct c2_bufvec_cursor *vc, void *val, ...);

/**
  Encode/Decode an array of arbitary elements into a bufvec.
  @param vc current position of the bufvec cursor
  @param p_arr pointer to the array.
  @param el_no number of elements.
  @param max_size max number of elements.
  @param el_size size in bytes of each element.
  @param el_proc The bufvec xcode procedure to handle the encode/decode of each
  element of the array.
  @param what The type of operation to be performed on the array-encode or
  decode.
  @retval 0 on success.
  @retval -errno on failure.
*/
int c2_bufvec_array(struct c2_bufvec_cursor *vc, void *p_arr, uint64_t el_no,
		    size_t max_size, size_t el_size, c2_bufvec_xcode_t el_proc,
		    enum c2_bufvec_what what);

/**
  Encode/Decode a sequence of bytes into a bufvec. For proper alignment,
  padding bytes are added at the end of the sequence if the size is not a
  multiple of 8 bytes during encode. This ensures that the bufvec cursor
  always has an 8 byte aligned address.
  @param vc current position of the bufvec cursor.
  @param cpp pointer to the byte array.
  @param size  count of bytes to be encoded/decoded.
  @param max_size max no of bytes that can be encoded/decoded.
  @param what The type of operation to be performed - encode or decode.
  @retval 0 on success.
  @retval -errno on failure.
*/
int c2_bufvec_bytes(struct c2_bufvec_cursor *vc, char **cpp, size_t size,
		    size_t max_size, enum c2_bufvec_what what);

/** @} end of bufvec group */

/**
  Calculates the onwire size of fop data. This function internally calls
  the fop field type specific functions to calculate the size

  @param fop The data for this fop is to be encoded/decoded.

  @retval Onwire size of the fop in bytes.
*/
size_t c2_xcode_fop_size_get(struct c2_fop *fop);

/** Get the pad bytes required for message */
int c2_xcode_pad_bytes_get(size_t size);

/** Add zero padding to message */
int c2_xcode_zero_padding_add(struct c2_bufvec_cursor *cur, uint64_t pad_bytes);

#endif /* C2_BUFVEC_XCODE_H_ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
