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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/tx_group.h"
#include "be/tx.h"
#include "fop/fom.h"  /* m0_fom_wakeup */

/**
 * @addtogroup be
 *
 * @{
 */

M0_TL_DESCR_DEFINE(grp, "m0_be_tx_group::tg_txs", M0_INTERNAL,
		   struct m0_be_tx, t_group_linkage, t_magic,
		   M0_BE_TX_MAGIC, M0_BE_TX_GROUP_MAGIC);

M0_TL_DEFINE(grp, M0_INTERNAL, struct m0_be_tx);

M0_INTERNAL void tx_group_init(struct m0_be_tx_group *gr,
			       struct m0_stob *log_stob)
{
#if 0 /* Nikita's code. */
	M0_SET0(gr);
#else /*XXX*/
	int rc;

	gr->tg_lsn = 0ULL;
	m0_be_tx_credit_init(&gr->tg_used);
	grp_tlist_init(&gr->tg_txs);
	gr->tg_opened = false; /* XXX because tx_fom.c:open_tick() */
	rc = m0_be_group_ondisk_init(&gr->tg_od, log_stob,
				     20, &M0_BE_TX_CREDIT(200000, 1ULL << 25));
	M0_ASSERT(rc == 0);
#endif
}

M0_INTERNAL void tx_group_fini(struct m0_be_tx_group *gr)
{
	m0_be_group_ondisk_fini(&gr->tg_od);
	grp_tlist_fini(&gr->tg_txs);
}

M0_INTERNAL void tx_group_add(struct m0_be_tx_engine *eng /* XXX unused */,
			      struct m0_be_tx_group *gr, struct m0_be_tx *tx)
{
	M0_ENTRY();

	tx->t_group  = gr;
	tx->t_leader = grp_tlist_is_empty(&gr->tg_txs);
	/* tx will be moved to M0_BTS_GROUPED state by an AST. */
	grp_tlist_add(&gr->tg_txs, tx);
	/* gr->tg_used.     XXX: what's here? */

	M0_LEAVE();
}

/**
 * Closes a transaction group.
 *
 * A group is closed when it either grows too large or becomes too old.
 */
M0_INTERNAL void
tx_group_close(struct m0_be_tx_engine *eng, struct m0_be_tx_group *gr)
{
	M0_ENTRY();
	/*
	 * A group is stored as a contiguous extent of the logical log with the
	 * following structure:
	 *
	 *         HEADER TX_HEADER* TX_BODY* COMMIT_BLOCK
	 *
	 * HEADER is struct tx_group_header in memory.
	 *
	 * COMMIT_BLOCK is struct tx_group_commit_block, duplicating HEADER
	 * fields (except magic).
	 *
	 * TX_HEADER is struct tx_group_entry in memory.
	 *
	 * When a group is closed, it builds its in-memory representation by
	 * filling group header and building an in-memory representation of
	 * transaction headers.
	 *
	 * This representation is build in pre-allocated (during tx engine
	 * initialisation) arrays of some maximal size. This size determines
	 * maximal group size (m0_be_log::lg_gr_size_max, maybe this should be
	 * changed into m0_be_tx_credit to account both for total size of
	 * transactions and the number of regions in them).
	 *
	 * tx_reg_header-s are built by scanning m0_be_tx::t_reg_d_area-s of
	 * group's transactions (skipping unused regions descriptors).
	 *
	 * Once tx_group_header and the array of tx_group_entry is built, it is
	 * encoded, by means of xcode, in a single sufficiently large and
	 * properly aligned buffer pre-allocated at transaction engine startup.
	 *
	 * Note that tx_reg_header::rh_lsn is the lsn of the corresponding
	 * region contents in TX_BODY.
	 *
	 * Then, the group is written to the log: a bufvec for stobio is formed
	 * to contain HEADER, TX_HEADER*, regions data from
	 * m0_be_tx::t_reg_area-s. This bufvec is pre-allocated too.
	 *
	 * This IO is launched. States of group's trasaction are changed to
	 * SUBMITTED.  When it completes, another IO for COMMIT_BLOCK is
	 * launched. When this IO completes, transaction states are changed to
	 * LOGGED, m0_be_tx::t_persistent() is invoked.
	 *
	 * Then, in-place IO is submitted to segments, directly from
	 * m0_be_tx::t_reg_area-s, again using pre-allocated bufvec-s. In the
	 * simplest case, each transaction is submitted as a single IO,
	 * alternatively some grouping can be done. When in-place IO for a
	 * transaction completes, the transaction state changes to PLACED.
	 *
	 * When all group's transactions reach PLACED state, the group is
	 * finalised.
	 */

	/* XXX TODO ... */

	gr->tg_opened = false;
	/* m0_fom_wakeup(eng->te_fom); */

	M0_LEAVE();
}

M0_INTERNAL void
tx_group_open(struct m0_be_tx_engine *eng, struct m0_be_tx_group *gr)
{
	M0_ENTRY();
	M0_PRE(grp_tlist_is_empty(&gr->tg_txs));

	gr->tg_opened = true;
	gr->tg_fom = eng->te_fom;

	M0_LEAVE();
}

/** @} end of be group */
#undef M0_TRACE_SUBSYSTEM

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
