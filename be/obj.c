/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Valery V. Vorotyntsev <valery.vorotyntsev@seagate.com>
 * Original creation date: 22-Dec-2014
 */

#include "be/obj.h"
#include "lib/assert.h"  /* M0_PRE */

/**
 * @addtogroup be_obj
 *
 * @{
 */

M0_INTERNAL void m0_be_obj_header_pack(struct m0_be_obj_header *dest,
				       const struct m0_be_obj_tag *src)
{
	M0_PRE(_0C(src->ot_version >> 16 == 0) && _0C(src->ot_size >> 16 == 0));
	dest->hd_bits = (uint64_t)src->ot_version << 48 | src->ot_type << 16 |
		src->ot_size;
}

M0_INTERNAL void m0_be_obj_header_unpack(struct m0_be_obj_tag *dest,
					 const struct m0_be_obj_header *src)
{
	*dest = (struct m0_be_obj_tag){
		.ot_version = src->hd_bits >> 48,
		.ot_type    = src->hd_bits >> 16 & 0xffffffff,
		.ot_size    = src->hd_bits & 0xffff
	};
}

/** @} be_obj */
