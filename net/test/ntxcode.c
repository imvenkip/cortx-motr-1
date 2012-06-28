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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 06/28/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* @todo remove */
#ifndef __KERNEL__
#include <stdio.h>		/* printf */
#endif

#include "lib/cdefs.h"		/* container_of */
#include "lib/types.h"		/* c2_bcount_t */
#include "lib/misc.h"		/* C2_SET0 */
#include "lib/memory.h"		/* c2_alloc */
#include "lib/errno.h"		/* ENOMEM */

#include "net/test/ntxcode.h"

#ifndef __KERNEL__
#define LOGD(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOGD(format, ...) do {} while (0)
#endif

/**
   @defgroup NetTestXCODEInternals Colibri Network Bencmark Xcode internals.

   @see
   @ref net-test

   @{
 */

#if 0
/**
   Encode/decode object field to buffer.
   @param bv_length total length of bv.
	            Must be equivalent to c2_vec_count(&bv->ov_vec).
   @return 0 No space in buffer.
   @return >0 Number of bytes written to buffer.
   @see transform().
 */
static c2_bcount_t transform_single(enum net_test_transform_op op,
				    void *obj,
				    struct net_test_descr *descr,
				    struct c2_bufvec *bv,
				    c2_bcount_t bv_offset,
				    c2_bcount_t bv_length)
{
	return 0;
}

/**
   Encode or decode data structure with the given description.
   @param op operation. Can be NET_TEST_ENCODE or NET_TEST_DECODE.
   @param obj pointer to data structure.
   @param descr array of data field descriptions.
   @param descr_nr described fields number in descr.
   @return 0 No space in buffer.
   @return >0 Number of bytes written to buffer.
   @param bv c2_bufvec. Can be NULL - in this case bv_offset and bv_length
	     are ignored.
   @param bv_offset offset in bv.
   @see transform_single().
 */
/** @todo make static */
//static
c2_bcount_t transform(enum net_test_transform_op op,
			     void *obj,
			     struct net_test_descr *descr,
			     size_t descr_nr,
			     struct c2_bufvec *bv,
			     c2_bcount_t bv_offset)
{
	c2_bcount_t len_total = 0;
	c2_bcount_t len_current = 0;
	c2_bcount_t bv_length = bv == NULL ? 0 : c2_vec_count(&bv->ov_vec);
	size_t	    i;

	for (i = 0; i < descr_nr; ++i) {
		len_current = transform_single(op, obj, &descr[i],
					       bv, bv_offset + len_total,
					       bv_length);
		len_total += len_current;
		if (len_current == 0)
			break;
	}
	return len_current == 0 ? 0 : len_total;
}
#endif

/**
   @} end NetTestXCODEInternals
 */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
