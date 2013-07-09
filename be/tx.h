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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 29-May-2013
 */

#pragma once
#ifndef __MERO_BE_TX_H__
#define __MERO_BE_TX_H__

#include "be/log.h"
#include "be/tx_log.h"
#include "be/tx_group.h"
#include "be/tx_regmap.h"

struct m0_ref;

/**
 * @defgroup be
 *
 * Transaction engine.
 *
 * Main abstractions provided by this module are:
 *
 *     - transaction m0_be_tx: is a group of updates to BE segment memory,
 *       atomic w.r.t. BE failures. A transaction can update memory within
 *       multiple segments in the same m0_be instance. A BE user creates a
 *       transaction and then updates segment memory. After each update, the
 *       updated memory is "captured" in the transaction by calling
 *       m0_be_tx_capture();
 *
 *     - transaction credit m0_be_tx_credit: an object describing an update to
 *       segment memory that a transaction could make. Before a memory update
 *       can be captured in a transaction, the transaction should be "prepared"
 *       (m0_be_tx_prep()) for all forthcoming captures. This preparation
 *       reserves internal transaction resources (log space and memory) to avoid
 *       dead-locks;
 *
 *     - transaction engine m0_be_tx_engine: is a part of BE instance (m0_be)
 *       that contains all transaction related state.
 *
 * Overview of operation.
 *
 * When a memory region is captured in a transaction, the contents of this
 * region, i.e., new values placed in the memory by the user, are copied in a
 * transaction-private memory buffer. Eventually the transaction is closed,
 * i.e., the user indicates that no more updates will be captured in the
 * transaction. Closed transactions are collected in "transaction groups"
 * (m0_be_tx_group), which are units of IO. When a group is formed it is written
 * to the log. When log IO for the group completes, transactions from the group
 * are written "in-place", that is, their captured updates are written to the
 * corresponding places in the segment storage. Some time after a transaction is
 * written in-place, it is discarded and the space it uses in the log is
 * reclaimed.
 *
 * Notes:
 *
 *     - transaction engine implements redo-only WAL (write-ahead logging).
 *       There is no way to abort a transaction;
 *
 *     - serializibility and concurrency concerns are delegated to a user.
 *       Specifically, the user must call m0_be_tx_capture() while the lock,
 *       protecting the memory being captured is held. In the current
 *       implementation this lock must be held at least until the transaction
 *       is closed, in the future this requirement will be weakened;
 *
 *     - currently, the transaction engine writes modified memory in place,
 *       as described in the "Overview of operation" section. In the future,
 *       the transaction engine would leave this task to the (currently
 *       non-existing) segment page daemon;
 *
 *     - transaction close call (m0_be_tx_close()) does not guarantee
 *       transaction persistence. Transaction will become persistent later.
 *       The user can set a call-back m0_be_tx::t_persistent() that is called
 *       when the transaction becomes persistent;
 *
 *     - transactions become persistent in the same order as they close.
 *
 * @{
 */

/**
 * Transaction state machine.
 *
 * @verbatim
 *
 *                        | m0_be_tx_init()
 *                        |
 *                        V
 *                     PREPARE------+
 *                        | ^       | m0_be_tx_prep();
 *        m0_be_tx_open() | |       |
 *                        | +-------+
 *                        V
 *                     OPENING---------->FAILED
 *                        |
 *                        | tx_open_tail()
 *                        |
 *                        V
 *                      ACTIVE------+
 *                        | ^       | m0_be_tx_capture();
 *       m0_be_tx_close() | |       |
 *                        | +-------+
 *                        V
 *                      CLOSED
 *                        |
 *                        | tx_group_add()
 *                        |
 *                        V
 *                     GROUPED
 *                        |
 *                        | log io & in-place io complete
 *                        |
 *                        V
 *                      PLACED
 *                        |
 *                        | m0_be_tx_stable()
 *                        |
 *                        V
 *                      DONE
 *
 * @endverbatim
 *
 * A transaction goes through the states sequentially. The table below
 * corresponds to sequence of transactions in the system history. An individual
 * transaction, as it gets older, moves through this table bottom-up.
 *
 * @verbatim
 *
 *      transaction          log record
 *         state                state
 * |                    |                     |
 * |                    |                     |
 * | DONE               |  record discarded   |
 * | (updates in place) |                     |
 * |                    |                     |
 * +--------------------+---------------------+----> start
 * |                    |                     |
 * | PLACED             |  persistent         |
 * | (updates in place  |                     |
 * |  and in log)       |                     |
 * |                    |                     |
 * +--------------------+---------------------+----> placed
 * |                    |                     |
 * | ``LOGGED''         |  persistent         |
 * | (updates in log)   |                     |
 * |                    |                     |
 * +--------------------+---------------------+----> logged
 * |                    |                     |
 * | ``SUBMITTED''      |  in flight          |
 * | (updates in flight |                     |
 * |  to log)           |                     |
 * |                    |                     |
 * +--------------------+---------------------+----> submitted
 * |                    |                     |
 * | GROUPED            |  in memory,         |
 * | (grouped)          |  log location       |
 * |                    |  assigned           |
 * |                    |                     |
 * +--------------------+---------------------+----> grouped
 * |                    |                     |
 * | CLOSED             |  in memory,         |
 * | (ungrouped)        |  log location       |
 * |                    |  not assigned       |
 * |                    |                     |
 * +--------------------+---------------------+----> inmem
 * |                    |                     |
 * | ACTIVE             |  in memory,         |
 * | (capturing         |  log space          |
 * |  updates)          |  reserved           |
 * |                    |                     |
 * +--------------------+---------------------+----> prepared
 * |                    |                     |
 * | OPENING            |  no records         |
 * | (waiting for log   |                     |
 * |  space             |                     |
 * |                    |                     |
 * +--------------------+---------------------+
 * |                    |                     |
 * | PREPARE            | no records          |
 * | (accumulating      |                     |
 * |  credits)          |                     |
 * +--------------------+---------------------+
 *
 * @endverbatim
 *
 */
enum m0_be_tx_state {
	/**
	 * Transaction failed. It cannot be used further and should be finalised
	 * (m0_be_tx_fini()).
	 *
	 * Currently, the only way a transaction can reach this state is by
	 * failing to allocate internal memory in m0_be_tx_open() call or by
	 * growing too large (larger than the total log space) in prepare state.
	 */
	M0_BTS_FAILED,
	/**
	 * State in which transaction is being prepared to opening; initial
	 * state after m0_be_tx_init().
	 *
	 * In this state, m0_be_tx_prep() calls should be made to reserve
	 * internal resources for the future captures. It is allowed to prepare
	 * for more than will be actually captured: typically it is impossible
	 * to precisely estimate updates that will be done as part of
	 * transaction, so a user should conservatively prepare for the
	 * worst-case.
	 */
	M0_BTS_PREPARE,
	/**
	 * In this state transaction waits for internal resource to be
	 * allocated.
	 *
	 * Specifically, the transaction is in this state until there is enough
	 * free space in the log to store transaction updates.
	 */
	M0_BTS_OPENING,
	/**
	 * In this state transaction is used to capture updates.
	 */
	M0_BTS_ACTIVE,
	/**
	 * Transaction is closed, but not yet grouped.
	 */
	M0_BTS_CLOSED,
	/**
	 * Transaction is a member of transaction group.
	 */
	M0_BTS_GROUPED,
	/**
	 * All transaction in-place updates completed.
	 */
	M0_BTS_PLACED,
	/**
	 * Transaction was declared stable by call to m0_be_tx_stable().
	 */
	M0_BTS_DONE,
	M0_BTS_NR
};

/*
 * NOTE: Call-backs of this type must be asynchronous, because they can be
 * called from state transition functions.
 */
typedef void (*m0_be_tx_cb_t)(const struct m0_be_tx *tx);

/**
 * Transaction engine. Embedded in m0_be.
 */
struct m0_be_tx_engine {
	/**
	 * Per-state lists of transaction. Each non-failed transaction is in one
	 * of these lists.
	 */
	struct m0_tl          te_txs[M0_BTS_NR];

	/** Protects all fields of this struct. */
	struct m0_rwlock      te_lock;

	/** Transactional log. */
	struct m0_be_log      te_log;

	/** Transactional group. (Currently there is only one.) */
	struct m0_be_tx_group te_group;

	/*
	 * Various interesting positions in the log. XXX Probably not needed.
	 */
	struct m0_be_tx      *te_start;
	struct m0_be_tx      *te_placed;
	struct m0_be_tx      *te_logged;
	struct m0_be_tx      *te_submitted;
	struct m0_be_tx      *te_inmem;

	/**
	 * Total space reserved for active transactions.
	 *
	 * This space is reserved by m0_tx_open(). When a transaction closes
	 * (m0_be_tx_close()), the difference between reserved and actually used
	 * space is released.
	 */
	m0_bcount_t           te_reserved;

	/** Pointer to the FOM processing transaction groups. */
	struct m0_fom        *te_fom;
};

M0_INTERNAL bool
m0_be__tx_engine_invariant(const struct m0_be_tx_engine *engine);

M0_INTERNAL void m0_be_tx_engine_init(struct m0_be_tx_engine *engine);
M0_INTERNAL void m0_be_tx_engine_fini(struct m0_be_tx_engine *engine);

/**
 * Transaction.
 */
struct m0_be_tx {
	uint64_t               t_magic;
	struct m0_sm           t_sm;
	struct m0_sm_ast       t_ast;

	/** Transaction identifier, assigned by the user. */
	uint64_t               t_id;
	/**
	 * lsn of transaction representation in the log. Assigned when the
	 * transaction reaches GROUPED state.
	 */
	uint64_t               t_lsn;

	struct m0_be          *t_be;
	/** Linkage in one of m0_be_tx_engine::te_txs[] lists. */
	struct m0_tlink        t_engine_linkage;

	/**
	 * The group the transaction is part of. This is non-NULL iff the
	 * transaction is in GROUPED or later state.
	 */
	struct m0_be_tx_group *t_group;
	/** Linkage in m0_be_tx_group::tg_txs. */
	struct m0_tlink        t_group_linkage;

	/**
	 * Size (in bytes) of "payload area" in the transaction log header,
	 * reserved for user.
	 *
	 * User should directly set this field, while the transaction is in
	 * ACTIVE state.
	 */
	m0_bcount_t            t_payload_size;

	/** Updates prepared for at PREPARE state. */
	struct m0_be_tx_credit t_prepared;
	struct m0_be_reg_area  t_reg_area;

	/**
	 * True iff the transaction is the first transaction in the group. In
	 * this case, the overhead of group (specifically, the size of group
	 * header and group commit log) are "billed" to the transaction.
	 */
	bool                   t_leader;

	/**
	 * Optional call-back called when the transaction is guaranteed to
	 * survive all further failures. This is invoked upon log IO
	 * completion.
	 */
	m0_be_tx_cb_t          t_persistent;

	/**
	 * This optional call-back is called when a stable transaction is about
	 * to be discarded from the history.
	 *
	 * A typical user of this call-back is ioservice that uses ->t_discarded
	 * to initiate a new transaction to free storage space used by the
	 * COW-ed file extents.
	 */
	m0_be_tx_cb_t          t_discarded;

	/**
	 * True iff the transaction is globally stable, i.e., not
	 * needed for recovery.
	 */
	bool                   t_glob_stable;

	/**
	 * An optional call-back called when the transaction is being closed.
	 *
	 * "payload" parameter is the pointer to a m0_be_tx::t_payload_size-d
	 * buffer, that will be written to the log.
	 *
	 * ->t_filler() can capture regions in the transaction.
	 *
	 * A typical use of this call-back is to form a "fol record" used by DTM
	 * for distributed transaction management.
	 */
	void                 (*t_filler)(struct m0_be_tx *tx, void *payload);

	/**
	 * User-specified value, associated with the transaction. Transaction
	 * engine doesn't interpret this value. It can be used to pass
	 * additional information to the call-backs.
	 */
	void                  *t_datum;
};

M0_INTERNAL bool m0_be__tx_invariant(const struct m0_be_tx *tx);

M0_INTERNAL void m0_be_tx_init(struct m0_be_tx    *tx,
			       uint64_t            tid,
			       struct m0_be       *be,
			       struct m0_sm_group *sm_group,
			       m0_be_tx_cb_t       persistent,
			       m0_be_tx_cb_t       discarded,
			       bool                is_part_of_global_tx,
			       void              (*filler)(struct m0_be_tx *tx,
							   void *payload),
			       void               *datum);

M0_INTERNAL void m0_be_tx_fini(struct m0_be_tx *tx);

M0_INTERNAL void m0_be_tx_prep(struct m0_be_tx *tx,
			       const struct m0_be_tx_credit *credit);

M0_INTERNAL void m0_be_tx_open(struct m0_be_tx *tx);

M0_INTERNAL void m0_be_tx_capture(struct m0_be_tx *tx,
				  const struct m0_be_reg *reg);
M0_INTERNAL void m0_be_tx_uncapture(struct m0_be_tx *tx,
				    const struct m0_be_reg *reg);

#define M0_BE_TX_CAPTURE_PTR(seg, tx, ptr) \
	m0_be_tx_capture((tx), &M0_BE_REG((seg), sizeof *(ptr), (ptr)))

M0_INTERNAL void m0_be_tx_close(struct m0_be_tx *tx);

/** Forces the transaction to storage. */
M0_INTERNAL void m0_be_tx_force(struct m0_be_tx *tx);

M0_INTERNAL int m0_be_tx_timedwait(struct m0_be_tx *tx, int state,
				   m0_time_t timeout);

/** Notifies backend that the transaction is no longer needed for recovery. */
M0_INTERNAL void m0_be_tx_stable(struct m0_be_tx *tx);

M0_INTERNAL void m0_be__tx_state_set(struct m0_be_tx *tx,
				     enum m0_be_tx_state state);
M0_INTERNAL enum m0_be_tx_state m0_be__tx_state(const struct m0_be_tx *tx);

/**
 * Posts an AST that will move transaction's state machine to
 * M0_BTS_GROUPED state and decrement provided reference counter.
 */
M0_INTERNAL void m0_be__tx_group_post(struct m0_be_tx *tx, struct m0_ref *ref);
M0_INTERNAL void m0_be__tx_placed_post(struct m0_be_tx *tx, struct m0_ref *ref);

M0_TL_DESCR_DECLARE(eng, M0_EXTERN);
M0_TL_DECLARE(eng, M0_INTERNAL, struct m0_be_tx);

/** @} end of be group */
#endif /* __MERO_BE_TX_H__ */

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
