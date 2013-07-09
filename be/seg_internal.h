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

/**
 * @defgroup be
 *
 * @{
 */

enum {
	/** Dictionary entity name maximum string size. */
	M0_BE_SEG_DICT_MAXNAME = 80,
	/** Maximum number of entities in dictionary. */
	M0_BE_SEG_DICT_SIZE = 32
};

struct m0_be_seg_dict {
	char  bsd_name[M0_BE_SEG_DICT_MAXNAME];
	void *bsd_ptr;
};

/** "On-disk" header for segment, stored in STOB at zero offset */
struct m0_be_seg_hdr {
	void                         *bh_addr;  /**< Segment address in RAM. */
	m0_bcount_t                   bh_size;  /**< Segment size. */
	struct m0_be_allocator_header bh_alloc;
	/** Segment dictionary */
	struct m0_be_seg_dict         bs_dict[M0_BE_SEG_DICT_SIZE];
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
