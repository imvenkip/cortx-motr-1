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

#ifndef __MERO_BE_LOG_H__
#define __MERO_BE_LOG_H__

#include "be/log_store.h"
#include "be/tx_group.h"

/**
 * @defgroup be
 *
 * @{
 */

struct m0_be_log;
typedef void (*m0_be_log_got_space_cb_t)(struct m0_be_log *log);

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
struct m0_be_log {
	/**
	 * Underlying storage.
	 *
	 * @todo this might be changed to something more complicated to support
	 * flexible deployment and grow-able logs. E.g., a log can be stored in
	 * a sequence of regions in segments, linked to each other through
	 * header blocks.
	 */
	struct m0_be_log_store	 lg_stor;

	/**
	 * lsn to be used for the next log element.
	 */
	m0_bindex_t		 lg_lsn;
	m0_be_log_got_space_cb_t lg_got_space_cb;
};

M0_INTERNAL void m0_be_log_init(struct m0_be_log *log,
				m0_be_log_got_space_cb_t got_space_cb);
M0_INTERNAL void m0_be_log_fini(struct m0_be_log *log);
M0_INTERNAL bool m0_be_log__invariant(struct m0_be_log *log);

M0_INTERNAL int  m0_be_log_open(struct m0_be_log *log);
M0_INTERNAL void m0_be_log_close(struct m0_be_log *log);

M0_INTERNAL int  m0_be_log_create(struct m0_be_log *log, m0_bcount_t log_size);
M0_INTERNAL void m0_be_log_destroy(struct m0_be_log *log);

M0_INTERNAL struct m0_stob *m0_be_log_stob(struct m0_be_log *log);

M0_INTERNAL void m0_be_log_cblock_credit(struct m0_be_tx_credit *credit,
					 m0_bcount_t cblock_size);

M0_INTERNAL int m0_be_log_submit(struct m0_be_log *log,
				 struct m0_be_op *op,
				 struct m0_be_tx_group *group);

M0_INTERNAL int m0_be_log_commit(struct m0_be_log *log,
				 struct m0_be_op *op,
				 struct m0_be_tx_group *group);

/*
M0_INTERNAL void m0_be_log_discard(struct m0_be_log *log,
				   struct m0_be_group_ondisk *group);
*/
/* XXX */
M0_INTERNAL void m0_be_log_discard(struct m0_be_log *log,
				   struct m0_be_tx_credit *reserved);

M0_INTERNAL int m0_be_log_reserve_tx(struct m0_be_log *log,
				     struct m0_be_tx_credit *prepared);

/** @} end of be group */

#endif /* __MERO_BE_LOG_H__ */


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
