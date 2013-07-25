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

#include "lib/errno.h"		/* ENOSPC */

#include "be/tx.h"		/* m0_be_tx__reg_area */
#include "be/log.h"		/* m0_be_log_stob */
#include "be/engine.h"		/* m0_be_engine__tx_group_open */

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
	int rc;

	gr->tg_lsn = 0ULL;
	m0_be_tx_credit_init(&gr->tg_used);
	grp_tlist_init(&gr->tg_txs);
	rc = m0_be_group_ondisk_init(&gr->tg_od, log_stob,
				     20, &M0_BE_TX_CREDIT(200000, 1ULL << 25));
	M0_ASSERT(rc == 0);
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

	/* XXX TODO? */

	M0_LEAVE();
}

M0_INTERNAL void m0_be_tx_group_stable(struct m0_be_tx_group *gr)
{
	M0_ENTRY();
	m0_be_tx_group_fom_stable(&gr->tg_fom);
	M0_LEAVE();
}

M0_INTERNAL void m0_be_tx_group_close(struct m0_be_tx_group *gr)
{
	M0_ENTRY();
	m0_be_tx_group_fom_handle(&gr->tg_fom, gr);
	M0_LEAVE();
}

M0_INTERNAL void m0_be_tx_group_reset(struct m0_be_tx_group *gr)
{
	M0_PRE(grp_tlist_is_empty(&gr->tg_txs));
	M0_PRE(gr->tg_nr_unstable == 0);

	gr->tg_used = M0_BE_TX_CREDIT(0, 0);
	m0_be_group_ondisk_reset(&gr->tg_od);
	m0_be_tx_group_fom_reset(&gr->tg_fom);
}

M0_INTERNAL int m0_be_tx_group_init(struct m0_be_tx_group *gr,
				    struct m0_be_tx_credit *size_max,
				    size_t tx_nr_max,
				    struct m0_be_engine *en,
				    struct m0_be_log *log,
				    struct m0_reqh *reqh)
{
	int rc;

	*gr = (struct m0_be_tx_group) {
		.tg_size      = *size_max,
		.tg_tx_nr_max = tx_nr_max,
		.tg_log       = log,
		.tg_engine    = en,
	};
	grp_tlist_init(&gr->tg_txs);
	/* XXX make the same paremeters order for m0_be_tx_group_ondisk */
	rc = m0_be_group_ondisk_init(&gr->tg_od, m0_be_log_stob(log),
				     tx_nr_max, &gr->tg_size);
	if (rc == 0) {
		m0_be_tx_group_fom_init(&gr->tg_fom, reqh);
		m0_be_tx_group_reset(gr);
	}
	return rc;
}

M0_INTERNAL void m0_be_tx_group_fini(struct m0_be_tx_group *gr)
{
	m0_be_tx_group_fom_fini(&gr->tg_fom);
	m0_be_group_ondisk_fini(&gr->tg_od);
	grp_tlist_fini(&gr->tg_txs);
}

M0_INTERNAL int
m0_be_tx_group_tx_add(struct m0_be_tx_group *gr, struct m0_be_tx *tx)
{
	struct m0_be_tx_credit group_used = gr->tg_used;
	struct m0_be_tx_credit tx_used;
	int		       rc;

	M0_ENTRY();

	m0_be_reg_area_used(m0_be_tx__reg_area(tx), &tx_used);
	m0_be_tx_credit_add(&group_used, &tx_used);

	if (m0_be_tx_credit_le(&group_used, &gr->tg_size) &&
	    m0_be_tx_group_size(gr) < gr->tg_tx_nr_max) {
		grp_tlink_init_at(tx, &gr->tg_txs);
		gr->tg_used = group_used;
		M0_CNT_INC(gr->tg_nr_unstable);
		rc = 0;
	} else {
		rc = -ENOSPC;
	}

	M0_RETURN(rc);
}

M0_INTERNAL void
m0_be_tx_group_tx_del(struct m0_be_tx_group *gr, struct m0_be_tx *tx)
{
	grp_tlink_del_fini(tx);
}

M0_INTERNAL void m0_be_tx_group_tx_del_all(struct m0_be_tx_group *gr)
{
	struct m0_be_tx *tx;

	M0_BE_TX_GROUP_TX_FORALL(gr, tx) {
		m0_be_tx_group_tx_del(gr, tx);
	} M0_BE_TX_GROUP_TX_ENDFOR;
}

M0_INTERNAL void m0_be_tx_group_open(struct m0_be_tx_group *gr)
{
	m0_be_engine__tx_group_open(gr->tg_engine, gr);
}

M0_INTERNAL void m0_be_tx_group_start(struct m0_be_tx_group *gr)
{
	m0_be_tx_group_fom_start(&gr->tg_fom);
}

M0_INTERNAL void m0_be_tx_group_stop(struct m0_be_tx_group *gr)
{
	m0_be_tx_group_fom_stop(&gr->tg_fom);
}

M0_INTERNAL void m0_be_tx_group_discard(struct m0_be_tx_group *gr)
{
	m0_be_log_discard(gr->tg_log, &gr->tg_log_reserved);
}

M0_INTERNAL size_t m0_be_tx_group_size(struct m0_be_tx_group *gr)
{
	return grp_tlist_length(&gr->tg_txs);
}

M0_INTERNAL void m0_be_tx_group__log(struct m0_be_tx_group *gr,
				     struct m0_be_op *op)
{
	int    rc;

	/** XXX FIXME move somewhere else */
	m0_be_group_ondisk_io_reserved(&gr->tg_od, gr, &gr->tg_log_reserved);
	/** XXX FIXME: write with single call to m0_be_log function */
	m0_be_log_submit(gr->tg_log, op, gr);
	rc = m0_be_op_wait(op);
	M0_ASSERT(rc == 0);
	M0_ASSERT(m0_be_op_state(op) == M0_BOS_SUCCESS);
	/* XXX dirty hack */
	m0_be_op_fini(op);
	m0_be_op_init(op);
	m0_be_log_commit(gr->tg_log, op, gr);
}

M0_INTERNAL void m0_be_tx_group__place(struct m0_be_tx_group *gr,
				       struct m0_be_op *op)
{
	m0_be_io_launch(&gr->tg_od.go_io_seg, op);
}

M0_INTERNAL void m0_be_tx_group__tx_state_post(struct m0_be_tx_group *gr,
					       enum m0_be_tx_state state)
{
	struct m0_be_tx *tx;

	M0_BE_TX_GROUP_TX_FORALL(gr, tx) {
		m0_be_tx__state_post(tx, state);
	} M0_BE_TX_GROUP_TX_ENDFOR;
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
