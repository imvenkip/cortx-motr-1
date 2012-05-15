/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
#include "lib/types.h"
#include "lib/vec.h"
#include "lib/assert.h"
#include "fop/fop.h"
#include "fop/fop_base.h"
#include "lib/errno.h"
#include "xcode/bufvec_xcode.h"

/**
   Returns no of padding bytes that would be needed to keep a cursor aligned
   at 8 byte boundary.
   @pre size > 0
*/
static int pad_bytes_get(size_t size)
{
	uint64_t	result;

	C2_PRE(size > 0);

	if (size <= BYTES_PER_XCODE_UNIT)
		return BYTES_PER_XCODE_UNIT - size;
	result = size & (BYTES_PER_XCODE_UNIT - 1);
	return result == 0 ? result : BYTES_PER_XCODE_UNIT - result;
}

/**
  Adds padding bytes to the c2_bufvec_cursor to keep it aligned at 8 byte
  boundaries.
*/
static int zero_padding_add(struct c2_bufvec_cursor *cur, uint64_t pad_bytes)
{
	void		*pad_p;
	uint64_t	 pad = 0;
	c2_bcount_t      count;

	C2_PRE(cur != NULL);
	C2_PRE(pad_bytes < BYTES_PER_XCODE_UNIT);

	pad_p = &pad;

	count = c2_bufvec_cursor_copyto(cur, pad_p, pad_bytes);
	if (count != pad_bytes)
		return -EFAULT;
	return 0;
}

static int bufvec_uint64_encode(struct c2_bufvec_cursor *cur, uint64_t *val)
{
	c2_bcount_t count;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	count = c2_bufvec_cursor_copyto(cur, val, sizeof *val);
	if (count != sizeof *val)
		return -EFAULT;
	return 0;
}

static int bufvec_uint64_decode(struct c2_bufvec_cursor *cur, uint64_t *val)
{
	c2_bcount_t count;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	count = c2_bufvec_cursor_copyfrom(cur, val, sizeof *val);
	if (count != sizeof *val)
		return -EFAULT;
	return 0;

}

static int bufvec_uint32_encode(struct c2_bufvec_cursor *cur, uint32_t *val)
{
	uint64_t	l_val;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	l_val = (uint64_t)*val;
	return bufvec_uint64_encode(cur, &l_val);
}

static int bufvec_uint32_decode(struct c2_bufvec_cursor *cur, uint32_t *val)
{
	uint64_t	l_val;
	int		rc;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	rc = bufvec_uint64_decode(cur, &l_val);
	*val = (uint32_t)l_val;
	return rc;
}

static int bufvec_uint16_encode(struct c2_bufvec_cursor *cur, uint16_t *val)
{
	uint64_t	l_val;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	l_val = (uint64_t)*val;
	return bufvec_uint64_encode(cur, &l_val);
}

static int bufvec_uint16_decode(struct c2_bufvec_cursor *cur, uint16_t *val)
{
	int		rc;
	uint64_t	l_val;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	rc = bufvec_uint64_decode(cur, &l_val);
	*val = (uint16_t)l_val;
	return rc;
}

static int bufvec_byte_encode(struct c2_bufvec_cursor *cur, uint8_t *val)
{
	uint64_t	l_val;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	l_val = (uint64_t)*val;
	return bufvec_uint64_encode(cur, &l_val);
}

static int bufvec_byte_decode(struct c2_bufvec_cursor *cur, uint8_t *val)
{
	int		rc;
	uint64_t	l_val;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	rc = bufvec_uint64_decode(cur, &l_val);
	*val = (uint8_t)l_val;
	return rc;
}

int c2_bufvec_byte(struct c2_bufvec_cursor *vc, uint8_t *val,
		     enum c2_bufvec_what what)
{
	int rc;

	C2_PRE(vc != NULL);
	C2_PRE(val != NULL);

	if (what == C2_BUFVEC_ENCODE)
		rc = bufvec_byte_encode(vc, val);
	else if (what == C2_BUFVEC_DECODE)
		rc = bufvec_byte_decode(vc, val);
	else
	    rc = -ENOSYS;

	return rc;
}

int c2_bufvec_uint16(struct c2_bufvec_cursor *vc, uint16_t *val,
		     enum c2_bufvec_what what)
{
	int rc;

	C2_PRE(vc != NULL);
	C2_PRE(val != NULL);

	if (what == C2_BUFVEC_ENCODE)
		rc = bufvec_uint16_encode(vc, val);
	else if( what == C2_BUFVEC_DECODE)
		rc = bufvec_uint16_decode(vc, val);
	else
	    rc = -ENOSYS;

	return rc;
}

int c2_bufvec_uint32(struct c2_bufvec_cursor *vc, uint32_t *val,
		     enum c2_bufvec_what what)
{
	int rc;

	C2_PRE(vc != NULL);
	C2_PRE(val != NULL);

	if (what == C2_BUFVEC_ENCODE)
		rc = bufvec_uint32_encode(vc, val);
	else if (what == C2_BUFVEC_DECODE)
		rc = bufvec_uint32_decode(vc, val);
	else
	    rc = -ENOSYS;

	return rc;
}

int c2_bufvec_uint64(struct c2_bufvec_cursor *vc, uint64_t *val,
		     enum c2_bufvec_what what)
{
	int rc;

	C2_PRE(vc != NULL);
	C2_PRE(val != NULL);

	if(what == C2_BUFVEC_ENCODE)
		rc = bufvec_uint64_encode(vc, val);
	else if(what == C2_BUFVEC_DECODE)
		rc = bufvec_uint64_decode(vc, val);
	else
		rc = -ENOSYS;
	return rc;
}

int c2_bufvec_array(struct c2_bufvec_cursor *vc, void *p_arr, uint64_t el_no,
		    size_t max_size, size_t el_size, c2_bufvec_xcode_t el_proc,
		    enum c2_bufvec_what what)
{
	int		 rc;
	int		 i;
	void		*bp;

	C2_PRE(vc != NULL);
	C2_PRE(p_arr != NULL);
	C2_PRE(el_no != 0 || el_size * el_no <= max_size);
	C2_PRE(el_proc != NULL);

	bp = p_arr;
	rc = c2_bufvec_uint64(vc, &el_no, what);
	if (rc != 0)
		return rc;

	for (i = 0; i < el_no; ++i) {
		rc = (*el_proc)(vc, bp, what);
		if (rc != 0)
			return rc;
		bp += el_size;
	}
	return rc;
}

int c2_bufvec_bytes(struct c2_bufvec_cursor *vc, char **cpp, size_t size,
		    size_t max_size, enum c2_bufvec_what what)
{
	int		 rc = 0;
	c2_bcount_t      count;
	uint64_t	 pad_bytes;
	char		*bp;
	bool		 eob;

	C2_PRE(vc != NULL);
	C2_PRE(cpp != NULL);
	C2_PRE(size != 0 || size <= max_size);

	bp = *cpp;
	C2_ASSERT(bp != NULL);
	if (what == C2_BUFVEC_ENCODE) {
		/* Encode the data + pad bytes into the bufvec */
		count = c2_bufvec_cursor_copyto(vc, bp, size);
		if (count != size)
			return -EFAULT;
		pad_bytes = pad_bytes_get(size);
		if (pad_bytes > 0)
			rc = zero_padding_add(vc, pad_bytes);
	} else if (what == C2_BUFVEC_DECODE) {
		/*
		   Decode the data in the bufvec and advance the cursor by
		   pad_bytes to keep the bufvec cursor aligned.
		*/
		count = c2_bufvec_cursor_copyfrom(vc, bp, size);
		if (count != size)
			return -EFAULT;
		pad_bytes = pad_bytes_get(size);
		if (pad_bytes > 0) {
			eob = c2_bufvec_cursor_move(vc, pad_bytes);
			C2_ASSERT(!eob);
		}
	} else
		rc = -ENOSYS;

	return rc;
}
/*
int c2_bufvec_fop(struct c2_bufvec_cursor *vc, struct c2_fop *fop,
		  enum c2_bufvec_what what)
{
	int			 rc;
	struct c2_fop_type	*ftype;

	C2_PRE(fop != NULL);
	C2_PRE(vc != NULL);

	ftype = fop->f_type;
	C2_ASSERT(ftype->ft_ops != NULL);

	if (what == C2_BUFVEC_ENCODE)
		rc = ftype->ft_ops->fto_bufvec_encode(fop, vc);

	else if (what == C2_BUFVEC_DECODE)
		rc = ftype->ft_ops->fto_bufvec_decode(fop, vc);
	else
		rc = -EFAULT;

	return rc;
}
*/
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

