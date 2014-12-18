/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 5-Jun-2013
 */

#pragma once
#ifndef __MERO_BE_SEG_INTERNAL_H__
#define __MERO_BE_SEG_INTERNAL_H__

#include "be/alloc_internal.h"	/* m0_be_allocator_header */
#include "be/btree.h"		/* m0_be_btree */

/**
 * @addtogroup be
 *
 * @{
 */

/** "On-disk" header for segment, stored in STOB at zero offset */
struct m0_be_seg_hdr {
	struct m0_be_obj_header       bh_header;
	void                         *bh_addr;  /**< Segment address in RAM. */
	m0_bcount_t                   bh_size;  /**< Segment size. */
	struct m0_be_allocator_header bh_alloc;
	struct m0_be_btree            bs_dict;  /**< Segment dictionary */
	struct m0_be_obj_footer       bh_footer;
};

/** @} end of be group */
#endif /* __MERO_BE_SEG_INTERNAL_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
