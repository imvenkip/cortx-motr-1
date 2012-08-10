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
#include "lib/types.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "fop/fop.h"
#include "xcode/bufvec_xcode.h"

static int bufvec_uint64_encode(struct c2_bufvec_cursor *cur, uint64_t *val)
{
	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	return c2_data_to_bufvec_copy(cur, val, sizeof *val);
}

static int bufvec_uint64_decode(struct c2_bufvec_cursor *cur, uint64_t *val)
{
	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	return c2_bufvec_to_data_copy(cur, val, sizeof *val);
}

static int bufvec_uint32_encode(struct c2_bufvec_cursor *cur, uint32_t *val)
{
	uint64_t l_val;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	l_val = (uint64_t)*val;
	return bufvec_uint64_encode(cur, &l_val);
}

static int bufvec_uint32_decode(struct c2_bufvec_cursor *cur, uint32_t *val)
{
	uint64_t l_val;
	int	 rc;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	rc = bufvec_uint64_decode(cur, &l_val);
	*val = (uint32_t)l_val;
	return rc;
}

static int bufvec_uint16_encode(struct c2_bufvec_cursor *cur, uint16_t *val)
{
	uint64_t l_val;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	l_val = (uint64_t)*val;
	return bufvec_uint64_encode(cur, &l_val);
}

static int bufvec_uint16_decode(struct c2_bufvec_cursor *cur, uint16_t *val)
{
	int	 rc;
	uint64_t l_val;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	rc = bufvec_uint64_decode(cur, &l_val);
	*val = (uint16_t)l_val;
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

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

