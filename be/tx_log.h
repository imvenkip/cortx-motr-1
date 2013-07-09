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
 * Original author: Maxim Medved <maxim_medved@xyratex.com>
 * Original creation date: 17-Jun-2013
 */

#pragma once
#ifndef __MERO_BE_TX_LOG_H__
#define __MERO_BE_TX_LOG_H__

#include "lib/types.h"  /* m0_bcount_t */
#include "stob/stob.h"  /* m0_stob, m0_stob_io */

struct m0_be_tx;
struct m0_be_tx_engine;
struct m0_be_tx_credit;

/**
 * @defgroup be
 *
 * @{
 */

struct tx_group_header_X {
	uint64_t gh_lsn;
	uint64_t gh_size;
	uint64_t gh_tx_nr;
	uint64_t gh_magic;
} M0_XCA_RECORD;

struct tx_group_commit_block_X {
	uint64_t gc_lsn;
	uint64_t gc_size;
	uint64_t gc_tx_nr;
	uint64_t gc_magic;
} M0_XCA_RECORD;

struct tx_reg_header_X {
	uint64_t rh_seg_id;
	uint64_t rh_offset;
	uint64_t rh_size;
	uint64_t rh_lsn;
} M0_XCA_RECORD;

struct tx_reg_sequence_X {
	uint64_t              rs_nr;
	struct tx_reg_header_X *rs_reg;
} M0_XCA_SEQUENCE;

struct tx_group_entry_X {
	uint64_t               ge_tid;
	uint64_t               ge_lsn;
	struct m0_buf          ge_payload;
	struct tx_reg_sequence_X ge_reg;
} M0_XCA_RECORD;

struct m0_be_log_stor_X {
	struct m0_stob    *ls_stob;
	struct m0_clink    ls_clink;

	struct m0_stob_io  ls_io;
	struct m0_indexvec ls_io_iv;
	struct m0_bufvec   ls_io_bv;
};

/**
 * This structure encapsulates internals of transactional log.
 *
 * Logically, a log is an infinite sequence of transaction groups. New groups
 * are added to the log as they are formed and old groups are retired, when
 * their transaction stabilise.
 *
 * Physically, the log is implemented as a circular buffer on persistent
 * storage. To make this implementation possible, transaction engine guarantees
 * that the used portion of infinite log is never larger than the physical log
 * size.
 *
 * A position in the log is identified by a "log sequence number" (lsn), which
 * is simply an offset in the logical log. lsn uniquely identifies a point in
 * system history.
 */
struct m0_be_log_X {
	/**
	 * Underlying storage.
	 *
	 * @todo this might be changed to something more complicated to support
	 * flexible deployment and grow-able logs. E.g., a log can be stored in
	 * a sequence of regions in segments, linked to each other through
	 * header blocks.
	 */
	struct m0_be_log_stor_X   lg_stor;

	/** Log size. */
	m0_bcount_t		lg_size;
	/**
	 * Maximal transaction group size.
	 *
	 * When a transaction group reaches this size, it is "closed" and new
	 * group starts forming.
	 */
	m0_bcount_t		lg_gr_size_max;
	/**
	 * Maximal number of regions in group.
	 */
	m0_bcount_t             lg_gr_reg_max;
	/**
	 * lsn to be used for the next log element.
	 */
	m0_bindex_t		lg_lsn;
	/**
	 * Group entry, encoded into tx log buffer.
	 */
	struct tx_group_entry_X	lg_grent;
	/**
	 * Group entry buffer, comitted into the log on disk.
	 */
	void                   *lg_grent_buf;
};

/*
M0_INTERNAL m0_bcount_t tx_log_size(const struct m0_be_tx *tx,
				    const struct m0_be_tx_credit *cr,
				    bool leader);
M0_INTERNAL m0_bcount_t tx_log_free_space(const struct m0_be_tx_engine *eng);
M0_INTERNAL m0_bcount_t tx_prepared_log_size(const struct m0_be_tx *tx);
M0_INTERNAL m0_bcount_t tx_group_header_size(m0_bcount_t tx_nr);

M0_INTERNAL void log_init(struct m0_be_log_X *log, m0_bcount_t log_size,
			  m0_bcount_t group_size, m0_bcount_t reg_max);

M0_INTERNAL int  log_open(struct m0_be_log_X *log);
M0_INTERNAL void log_close(struct m0_be_log_X *log);
*/


/** @} end of be group */
#endif /* __MERO_BE_TX_LOG_H__ */

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
