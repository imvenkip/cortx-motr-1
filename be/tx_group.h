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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 17-Jun-2013
 */

#pragma once
#ifndef __MERO_BE_TX_GROUP_H__
#define __MERO_BE_TX_GROUP_H__

#include "lib/tlist.h"		/* m0_tl */

#include "be/tx_credit.h"	/* m0_be_tx_credit */
#include "be/tx_group_ondisk.h"	/* m0_be_group_ondisk */
#include "be/tx_group_fom.h"	/* m0_be_group_fom */
#include "be/tx.h"		/* m0_be_tx_state */

struct m0_be_tx_engine;
struct m0_be_tx;

/**
 * @defgroup be
 *
 * @{
 */

/**
 * Transaction group is a collection of transactions, consecutive in closing
 * order, that are written to the log and recovered together.
 *
 * A new group is initially empty. It absorbs transactions as they are closed,
 * until the group either grows larger than m0_be_log::lg_gr_size_max or grows
 * older than XXX. At that point, the group is closed and a new group is opened
 * up to an upper limit (XXX) on groups number (1 is a possible and reasonable
 * such limit).
 *
 * Once a group is closed it constructs in memory its persistent representation,
 * consisting of group header, sequence of transaction representations and group
 * commit block. The memory for this representation is pre-allocated in
 * transaction engine to avoid dead-locks. Once representation is constructed,
 * it is written to the log in one or multiple asynchronous IOs. Before group
 * commit block is written, all other IOs for the group must complete. After
 * that, the commit block is written. Once commit block IO completes, it is
 * guaranteed that the entire group is on the log. Waiting for IO completion can
 * be eliminated by using (currently unimplemented) barrier interface provided
 * by m0_stob, or by placing in the commit block a strong checksum of group
 * representation (the latter approach allows to check whether the entire group
 * made it to the log).
 */
struct m0_be_tx_group {
	/** XXX lsn of transaction group header in the log. */
	m0_bindex_t               tg_lsn;

	/** Total size of all updates in all transactions in this group. */
	struct m0_be_tx_credit    tg_used;
	struct m0_be_tx_credit    tg_size;
	size_t			  tg_tx_nr;
	size_t			  tg_tx_nr_max;
	/** List of transactions in the group. */
	struct m0_tl              tg_txs;
	/** XXX DOCUMENTME */
	struct m0_be_group_ondisk tg_od;
	/** @note it is not just tg_fom, it is tg_group_fom */
	struct m0_be_tx_group_fom tg_group_fom;
};

/* XXX make m0_be_tx_group_cfg? */
M0_INTERNAL int m0_be_tx_group_init(struct m0_be_tx_group *gr,
				    struct m0_be_tx_credit *size_max,
				    size_t tx_nr_max,
				    struct m0_stob *log_stob,
				    struct m0_reqh *reqh);
M0_INTERNAL void m0_be_tx_group_fini(struct m0_be_tx_group *gr);
M0_INTERNAL void m0_be_tx_group__invariant(struct m0_be_tx_group *gr);
M0_INTERNAL void m0_be_tx_group_reset(struct m0_be_tx_group *gr);

M0_INTERNAL int m0_be_tx_group_add(struct m0_be_tx_group *gr,
				   struct m0_be_tx *tx);
M0_INTERNAL void m0_be_tx_group_del(struct m0_be_tx_group *gr,
				    struct m0_be_tx *tx);
M0_INTERNAL size_t m0_be_tx_group_size(struct m0_be_tx_group *gr);

/* fom control */
M0_INTERNAL void m0_be_tx_group_write(struct m0_be_tx_group *gr);
M0_INTERNAL void m0_be_tx_group_shutdown(struct m0_be_tx_group *gr);

/* called from fom */
/* m0_be_tx__state_post() to all transactions in group */
M0_INTERNAL void m0_be_tx_group__tx_state_post(struct m0_be_tx_group *gr,
					       enum m0_be_tx_state state);
M0_INTERNAL void m0_be_tx_group__log(struct m0_be_tx_group *gr,
				     struct m0_be_op *op);
M0_INTERNAL void m0_be_tx_group__place(struct m0_be_tx_group *gr,
				       struct m0_be_op *op);

M0_INTERNAL void tx_group_init(struct m0_be_tx_group *gr,
			       struct m0_stob *log_stob);
M0_INTERNAL void tx_group_fini(struct m0_be_tx_group *gr);

M0_INTERNAL void tx_group_add(struct m0_be_tx_engine *eng,
			      struct m0_be_tx_group *gr,
			      struct m0_be_tx *tx);
M0_INTERNAL void tx_group_close(struct m0_be_tx_engine *eng,
				struct m0_be_tx_group *gr);
/* Note the absence of tx_group_open(). */

#define M0_BE_TX_GROUP_TX_FORALL(gr, tx) \
	m0_tl_for(grp, &(gr)->tg_txs, (tx))

#define M0_BE_TX_GROUP_TX_ENDFOR m0_tl_endfor

M0_TL_DESCR_DECLARE(grp, M0_EXTERN);
M0_TL_DECLARE(grp, M0_INTERNAL, struct m0_be_tx);

/** @} end of be group */
#endif /* __MERO_BE_TX_GROUP_H__ */

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
