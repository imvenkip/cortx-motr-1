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
 * Original creation date: 4-Jul-2013
 */


#pragma once

#ifndef __MERO_BE_GROUP_ONDISK_INTERNAL_H__
#define __MERO_BE_GROUP_ONDISK_INTERNAL_H__


/**
 * @defgroup be
 *
 * @{
 */

struct tx_group_header {
	uint64_t gh_lsn;
	uint64_t gh_size;
	uint64_t gh_tx_nr;
	uint64_t gh_magic;
} M0_XCA_RECORD;

struct tx_group_commit_block {
	uint64_t gc_lsn;
	uint64_t gc_size;
	uint64_t gc_tx_nr;
	uint64_t gc_magic;
} M0_XCA_RECORD;

struct tx_reg_header {
	uint64_t rh_seg_id;
	uint64_t rh_offset;
	uint64_t rh_size;
	uint64_t rh_lsn;
} M0_XCA_RECORD;

struct tx_reg_sequence {
	uint64_t              rs_nr;
	struct tx_reg_header *rs_reg;
} M0_XCA_SEQUENCE;



/** @} end of be group */

#endif /* __MERO_BE_GROUP_ONDISK_INTERNAL_H__ */


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
