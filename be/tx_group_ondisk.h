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

#ifndef __MERO_BE_TX_GROUP_ONDISK_H__
#define __MERO_BE_TX_GROUP_ONDISK_H__

#include "be/io.h"		/* m0_be_io */
#include "be/tx_regmap.h"	/* m0_be_reg_area */

/**
 * @defgroup be Meta-data back-end
 *
 * @{
 */

struct m0_be_tx_group;
struct m0_be_log;

/*
 * tx_group_header
 * gh_tx_nr X tx_group_entry
 * gh_reg_nr X tx_reg_header
 * XXX move it to _internal.h
 */
struct tx_reg_header {
	uint64_t rh_seg_id;
	uint64_t rh_offset;
	uint64_t rh_size;
	uint64_t rh_lsn;
} M0_XCA_RECORD;

struct tx_group_header {
	uint64_t gh_lsn;
	uint64_t gh_size;
	uint64_t gh_tx_nr;
	uint64_t gh_reg_nr;
	uint64_t gh_magic;
} M0_XCA_RECORD;

struct tx_group_entry {
	uint64_t               ge_tid;
	uint64_t               ge_lsn;
	struct m0_buf          ge_payload;
} M0_XCA_RECORD;

struct tx_group_commit_block {
	uint64_t gc_lsn;
	uint64_t gc_size;
	uint64_t gc_tx_nr;
	uint64_t gc_magic;
} M0_XCA_RECORD;

struct m0_be_group_ondisk {
	struct m0_be_io		      go_io_log;
	struct m0_be_io		      go_io_log_cblock;
	struct m0_be_io		      go_io_seg;
	struct m0_be_tx_credit	      go_io_cr_log;
	struct m0_be_tx_credit	      go_io_cr_log_cblock;
	struct m0_be_tx_credit	      go_io_cr_seg;
	struct m0_be_reg_area	      go_area;

	struct m0_be_tx_credit	      go_io_cr_log_reserved;

	struct tx_group_header	      go_header;
	struct tx_group_entry	     *go_entry;
	struct tx_reg_header	     *go_reg;
	struct tx_group_commit_block  go_cblock;
};

M0_INTERNAL void be_log_io_credit_tx(struct m0_be_tx_credit *io_tx,
				     const struct m0_be_tx_credit *prepared,
				     m0_bcount_t payload_size);

M0_INTERNAL void be_log_io_credit_group(struct m0_be_tx_credit *io_group,
					size_t tx_nr_max,
					const struct m0_be_tx_credit *prepared,
					m0_bcount_t payload_size);

M0_INTERNAL int m0_be_group_ondisk_init(struct m0_be_group_ondisk *go,
					struct m0_stob *log_stob,
					size_t tx_nr_max,
					const struct m0_be_tx_credit *size_max,
					uint64_t seg_nr_max);
M0_INTERNAL void m0_be_group_ondisk_fini(struct m0_be_group_ondisk *go);
M0_INTERNAL bool m0_be_group_ondisk__invariant(struct m0_be_group_ondisk *go);

M0_INTERNAL void m0_be_group_ondisk_reset(struct m0_be_group_ondisk *go);

M0_INTERNAL void m0_be_group_ondisk_reserved(struct m0_be_group_ondisk *go,
					     struct m0_be_tx_group *group,
					     struct m0_be_tx_credit *reserved,
					     m0_bcount_t *payload_size,
					     size_t *tx_nr);

M0_INTERNAL void m0_be_group_ondisk_io_reserved(struct m0_be_group_ondisk *go,
						struct m0_be_tx_group *group,
						struct m0_be_tx_credit *reserved);
M0_INTERNAL void m0_be_group_ondisk_serialize(struct m0_be_group_ondisk *go,
					      struct m0_be_tx_group *group,
					      struct m0_be_log *log);
/** @} end of be group */

#endif /* __MERO_BE_TX_GROUP_ONDISK_H__ */


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
