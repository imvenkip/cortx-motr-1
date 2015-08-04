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
 *                  Maxim Medved <max.medved@seagate.com>
 * Original creation date: 17-Jun-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/tx_group.h"

#include "lib/misc.h"        /* M0_SET0 */
#include "lib/errno.h"       /* ENOSPC */
#include "lib/memory.h"      /* M0_ALLOC_PTR */

#include "be/tx_internal.h"  /* m0_be_tx__reg_area */
#include "be/domain.h"       /* m0_be_domain_seg */
#include "be/engine.h"       /* m0_be_engine__tx_group_open */

/**
 * @addtogroup be
 *
 * @{
 */

M0_TL_DESCR_DEFINE(grp, "m0_be_tx_group::tg_txs", M0_INTERNAL,
		   struct m0_be_tx, t_group_linkage, t_magic,
		   M0_BE_TX_MAGIC, M0_BE_TX_GROUP_MAGIC);

M0_TL_DEFINE(grp, M0_INTERNAL, struct m0_be_tx);

/* TODO move comments to be/log.[ch] */
#if 0
M0_INTERNAL void tx_group_init(struct m0_be_tx_group *gr,
			       struct m0_stob *log_stob)
{
	struct m0_be_tx_credit cred = M0_BE_TX_CREDIT_INIT(200000, 1ULL << 25);
	int                    rc;

	M0_SET0(&gr->tg_used);
	grp_tlist_init(&gr->tg_txs);
	rc = m0_be_group_format_init(&gr->tg_od, log_stob, 20, &cred);
	M0_ASSERT(rc == 0);
}

M0_INTERNAL void tx_group_fini(struct m0_be_tx_group *gr)
{
	m0_be_group_format_fini(&gr->tg_od);
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
#endif

M0_INTERNAL void m0_be_tx_group_stable(struct m0_be_tx_group *gr)
{
	M0_ENTRY();
	m0_be_tx_group_fom_stable(&gr->tg_fom);
	M0_LEAVE();
}

M0_INTERNAL struct m0_sm_group *
m0_be_tx_group__sm_group(struct m0_be_tx_group *gr)
{
	return m0_be_tx_group_fom__sm_group(&gr->tg_fom);
}

static void be_tx_group_reg_area_rebuild(struct m0_be_reg_area *ra,
					 struct m0_be_reg_area *ra_new,
					 void                  *param)
{
	struct m0_be_engine *en = param;

	m0_be_engine__reg_area_lock(en);
	m0_be_engine__reg_area_rebuild(en, ra, ra_new);
	m0_be_engine__reg_area_prune(en);
	m0_be_engine__reg_area_unlock(en);
}

static void be_tx_group_reg_area_gather(struct m0_be_tx_group *gr)
{
	struct m0_be_tx *tx;

	/* Merge transactions reg_area */
	M0_BE_TX_GROUP_TX_FORALL(gr, tx) {
		++gr->tg_tx_nr;
	} M0_BE_TX_GROUP_TX_ENDFOR;

	/* XXX check if it's the right place */
	if (gr->tg_tx_nr > 0) {
		M0_BE_TX_GROUP_TX_FORALL(gr, tx) {
			m0_be_reg_area_merger_add(&gr->tg_merger,
						  m0_be_tx__reg_area(tx));
		} M0_BE_TX_GROUP_TX_ENDFOR;
		m0_be_reg_area_merger_merge_to(&gr->tg_merger, &gr->tg_reg_area);

		be_tx_group_reg_area_rebuild(&gr->tg_reg_area, &gr->tg_area_copy,
					     gr->tg_engine);
	}

	m0_be_reg_area_optimize(&gr->tg_reg_area);
}

static void be_tx_group_payload_gather(struct m0_be_tx_group *gr)
{
	/*
	 * In the future tx payload will be set via callbacks.
	 * This function will execute these callbacks to gather payload for
	 * each transaction from the group.
	 *
	 * Currently tx payload is filled before tx close, so this
	 * function does nothing.
	 */
}

/*
 * Fill m0_be_tx_group_format with data from the group.
 */
static void be_tx_group_deconstruct(struct m0_be_tx_group *gr)
{
	struct m0_be_fmt_tx  ftx;
	struct m0_be_reg_d  *rd;
	struct m0_be_tx     *tx;

	/* TODO add other fields */

	M0_BE_TX_GROUP_TX_FORALL(gr, tx) {
		m0_be_tx_deconstruct(tx, &ftx);
		m0_be_group_format_tx_add(&gr->tg_od, &ftx);
	} M0_BE_TX_GROUP_TX_ENDFOR;

	M0_BE_REG_AREA_FORALL(&gr->tg_reg_area, rd) {
		m0_be_group_format_reg_log_add(&gr->tg_od, rd);
		m0_be_group_format_reg_seg_add(&gr->tg_od, rd);
	};
}

M0_INTERNAL void m0_be_tx_group_close(struct m0_be_tx_group *gr)
{
	struct m0_be_tx *tx;
	m0_bcount_t      size_reserved = 0;

	M0_ENTRY("gr=%p recovering=%d", gr, (int)gr->tg_recovering);

	if (!gr->tg_recovering) {
		be_tx_group_reg_area_gather(gr);
		be_tx_group_payload_gather(gr);
		be_tx_group_deconstruct(gr);
		M0_BE_TX_GROUP_TX_FORALL(gr, tx) {
			size_reserved += tx->t_log_reserved_size;
		} M0_BE_TX_GROUP_TX_ENDFOR;
		m0_be_group_format_log_use(&gr->tg_od, size_reserved);
	}
	m0_be_tx_group_fom_handle(&gr->tg_fom);

	M0_LEAVE();
}

M0_INTERNAL void m0_be_tx_group_reset(struct m0_be_tx_group *gr)
{
	M0_PRE(grp_tlist_is_empty(&gr->tg_txs));
	M0_PRE(gr->tg_nr_unstable == 0);

	M0_SET0(&gr->tg_used);
	M0_SET0(&gr->tg_log_reserved);
	gr->tg_payload_prepared = 0;
	gr->tg_tx_nr            = 0;
	gr->tg_recovering       = false;
	m0_be_reg_area_reset(&gr->tg_reg_area);
	m0_be_reg_area_reset(&gr->tg_area_copy);
	m0_be_reg_area_merger_reset(&gr->tg_merger);
	m0_be_group_format_reset(&gr->tg_od);
	m0_be_tx_group_fom_reset(&gr->tg_fom);
}

M0_INTERNAL void m0_be_tx_group_init(struct m0_be_tx_group     *gr,
				     struct m0_be_tx_group_cfg *gr_cfg,
				     struct m0_be_tx_credit    *size_max,
				     size_t                     seg_nr_max,
				     size_t                     tx_nr_max,
				     struct m0_be_domain       *dom,
				     struct m0_be_engine       *en,
				     struct m0_be_log          *log,
				     struct m0_reqh            *reqh)
{
	int rc;

	*gr = (struct m0_be_tx_group) {
		.tg_cfg = {
			.tgc_format = {
				.gfc_fmt_cfg = {
					.fgc_tx_nr_max = tx_nr_max,
					.fgc_reg_nr_max = size_max->tc_reg_nr,
			/* XXX */	.fgc_payload_size_max = 0x200000,
					.fgc_reg_area_size_max =
						size_max->tc_reg_size,
					.fgc_seg_nr_max = seg_nr_max,
				},
				.gfc_seg_io_fdatasync = true,
			},
		},
		.tg_size             = *size_max,
		.tg_seg_nr_max       = seg_nr_max,
		.tg_payload_prepared = 0,
		.tg_tx_nr_max        = tx_nr_max,
		.tg_log              = log,
		.tg_domain           = dom,
		.tg_engine           = en,
	};
	grp_tlist_init(&gr->tg_txs);
	m0_be_tx_group_fom_init(&gr->tg_fom, gr, reqh);
	rc = m0_be_group_format_init(&gr->tg_od, &gr->tg_cfg.tgc_format,
				     gr, gr->tg_log);
	M0_ASSERT(rc == 0);	/* XXX */
	rc = m0_be_reg_area_init(&gr->tg_reg_area, size_max,
				 M0_BE_REG_AREA_DATA_NOCOPY);
	M0_ASSERT(rc == 0);	/* XXX */
	rc = m0_be_reg_area_init(&gr->tg_area_copy, size_max,
				 M0_BE_REG_AREA_DATA_NOCOPY);
	M0_ASSERT(rc == 0);     /* XXX */
	rc = m0_be_reg_area_merger_init(&gr->tg_merger, tx_nr_max);
	M0_ASSERT(rc == 0);     /* XXX */
}

M0_INTERNAL void m0_be_tx_group_fini(struct m0_be_tx_group *gr)
{
	m0_be_reg_area_merger_fini(&gr->tg_merger);
	m0_be_reg_area_fini(&gr->tg_area_copy);
	m0_be_reg_area_fini(&gr->tg_reg_area);
	m0_be_tx_group_fom_fini(&gr->tg_fom);
	grp_tlist_fini(&gr->tg_txs);
}

static void be_tx_group_tx_add(struct m0_be_tx_group *gr, struct m0_be_tx *tx)
{
	M0_LOG(M0_DEBUG, "tx=%p group=%p", tx, gr);
	grp_tlink_init_at_tail(tx, &gr->tg_txs);
	M0_CNT_INC(gr->tg_nr_unstable);
}

M0_INTERNAL int m0_be_tx_group_tx_add(struct m0_be_tx_group *gr,
				      struct m0_be_tx       *tx)
{
	struct m0_be_tx_credit  group_used = gr->tg_used;
	struct m0_be_tx_credit  tx_used;
	struct m0_be_tx_credit  tx_prepared;
	struct m0_be_tx_credit  tx_captured;
	struct m0_be_reg_area  *ra = m0_be_tx__reg_area(tx);
	int                     rc;

	M0_ENTRY();
	M0_PRE(equi(m0_be_tx__is_recovering(tx), gr->tg_recovering));

	if (m0_be_tx__is_recovering(tx)) {
		be_tx_group_tx_add(gr, tx);
		rc = 0; /* XXX check for ENOSPC */
	} else {
		m0_be_reg_area_used(ra, &tx_used);
		m0_be_tx_credit_add(&group_used, &tx_used);

		m0_be_reg_area_prepared(ra, &tx_prepared);
		m0_be_reg_area_captured(ra, &tx_captured);
		M0_LOG(M0_DEBUG, "tx = %p, prepared = "BETXCR_F", "
		       "captured = "BETXCR_F", "
		       "used = "BETXCR_F, tx, BETXCR_P(&tx_prepared),
		       BETXCR_P(&tx_captured), BETXCR_P(&tx_used));

		if (m0_be_tx_credit_le(&group_used, &gr->tg_size) &&
		    m0_be_tx_group_size(gr) < gr->tg_tx_nr_max) {
			be_tx_group_tx_add(gr, tx);
			gr->tg_used              = group_used;
			gr->tg_payload_prepared += tx->t_payload.b_nob;
			rc = 0;
		} else {
			rc = -ENOSPC;
		}
	}
	return M0_RC(rc);
}

M0_INTERNAL void m0_be_tx_group_tx_del(struct m0_be_tx_group *gr,
				       struct m0_be_tx *tx)
{
	M0_LOG(M0_DEBUG, "tx=%p gr=%p", tx, gr);
	grp_tlink_del_fini(tx);
}

M0_INTERNAL size_t m0_be_tx_group_tx_nr(struct m0_be_tx_group *gr)
{
	return grp_tlist_length(&gr->tg_txs);
}

M0_INTERNAL void m0_be_tx_group_open(struct m0_be_tx_group *gr)
{
	m0_be_engine__tx_group_open(gr->tg_engine, gr);
}

M0_INTERNAL int m0_be_tx_group_start(struct m0_be_tx_group *gr)
{
	return m0_be_tx_group_fom_start(&gr->tg_fom);
}

M0_INTERNAL void m0_be_tx_group_stop(struct m0_be_tx_group *gr)
{
	m0_be_tx_group_fom_stop(&gr->tg_fom);
}

M0_INTERNAL void m0_be_tx_group_discard(struct m0_be_tx_group *gr)
{
	m0_be_group_format_log_discard(&gr->tg_od);
}

M0_INTERNAL void m0_be_tx_group_engine_discard(struct m0_be_tx_group *gr)
{
	m0_be_engine__tx_group_discard(gr->tg_engine, gr);
}

M0_INTERNAL size_t m0_be_tx_group_size(struct m0_be_tx_group *gr)
{
	return grp_tlist_length(&gr->tg_txs);
}

M0_INTERNAL int m0_be_tx_group__allocate(struct m0_be_tx_group *gr)
{
	return m0_be_group_format_allocate(&gr->tg_od);
}

M0_INTERNAL void m0_be_tx_group__deallocate(struct m0_be_tx_group *gr)
{
	m0_be_group_format_fini(&gr->tg_od);
}

M0_INTERNAL void m0_be_tx_group_seg_place_prepare(struct m0_be_tx_group *gr)
{
	m0_be_group_format_seg_place_prepare(&gr->tg_od);
}

M0_INTERNAL void m0_be_tx_group_seg_place(struct m0_be_tx_group *gr,
					  struct m0_be_op       *op)
{
	m0_be_group_format_seg_place(&gr->tg_od, op);
}

M0_INTERNAL void m0_be_tx_group_encode(struct m0_be_tx_group *gr)
{
	m0_be_group_format_encode(&gr->tg_od);
}

M0_INTERNAL void m0_be_tx_group_log_write(struct m0_be_tx_group *gr,
					  struct m0_be_op       *op)
{
	m0_be_group_format_log_write(&gr->tg_od, op);
}

M0_INTERNAL void
m0_be_tx_group__tx_state_post(struct m0_be_tx_group *gr,
			      enum m0_be_tx_state    state,
			      bool                   del_tx_from_group)
{
	struct m0_be_tx *tx;

	M0_ENTRY("gr=%p state=%s group_size=%zd",
		 gr, m0_be_tx_state_name(state), m0_be_tx_group_size(gr));

	M0_ASSERT(m0_be_tx_group_size(gr) > 0);

	M0_BE_TX_GROUP_TX_FORALL(gr, tx) {
		if (del_tx_from_group)
			m0_be_tx_group_tx_del(gr, tx);
		m0_be_tx__state_post(tx, state);
	} M0_BE_TX_GROUP_TX_ENDFOR;

	M0_LEAVE();
}

M0_INTERNAL void m0_be_tx_group_recovery_prepare(struct m0_be_tx_group *gr,
						 struct m0_be_recovery *rvr)
{
	m0_be_group_format_recovery_prepare(&gr->tg_od, rvr);
	m0_be_tx_group_fom_recovery_prepare(&gr->tg_fom, rvr);
	gr->tg_recovering = true;
}

M0_INTERNAL void m0_be_tx_group_log_read(struct m0_be_tx_group *gr,
					 struct m0_be_op       *op)
{
	return m0_be_group_format_log_read(&gr->tg_od, op);
}

M0_INTERNAL int m0_be_tx_group_decode(struct m0_be_tx_group *gr)
{
	return m0_be_group_format_decode(&gr->tg_od);
}

static void be_tx_group_reconstruct_reg_area(struct m0_be_tx_group *gr)
{
	struct m0_be_group_format *gft = &gr->tg_od;
	struct m0_be_reg_d         rd;
	uint32_t                   reg_nr;
	uint32_t                   i;

	reg_nr = m0_be_group_format_reg_nr(gft);
	for (i = 0; i < reg_nr; ++i) {
		m0_be_group_format_reg_get(gft, i, &rd);
		m0_be_reg_area_capture(&gr->tg_reg_area, &rd);
		rd.rd_reg.br_seg = m0_be_domain_seg(gr->tg_domain,
						    rd.rd_reg.br_addr);
		m0_be_group_format_reg_seg_add(&gr->tg_od, &rd);
	}
}

/**
 * Highlighs:
 * - transactions are reconstructed in the given sm group;
 * - transactions are allocated using m0_alloc();
 * - transactions are freed using m0_be_tx_gc_enable().
 */
static void be_tx_group_reconstruct_transactions(struct m0_be_tx_group *gr,
						 struct m0_sm_group    *sm_grp)
{
	struct m0_be_group_format *gft = &gr->tg_od;
	struct m0_be_fmt_tx        ftx;
	struct m0_be_tx           *tx;
	uint32_t                   tx_nr;
	uint32_t                   i;
	int                        rc;

	tx_nr = m0_be_group_format_tx_nr(gft);
	for (i = 0; i < tx_nr; ++i) {
		M0_ALLOC_PTR(tx);
		M0_ASSERT(tx != NULL); /* XXX */
		m0_be_group_format_tx_get(gft, i, &ftx);
		m0_be_tx_init(tx, 0, gr->tg_domain,
			      sm_grp, NULL, NULL, NULL, NULL);
		/*
		 * XXX move this and tx open/close to the group fom
		 * XXX wait in group fom until all tx from group are GCed
		 */
		m0_be_tx_gc_enable(tx, NULL, NULL);
		m0_be_tx__group_assign(tx, gr);
		m0_be_tx__recovering(tx);
		m0_be_tx_reconstruct(tx, &ftx);
		/*
		 * XXX it is possible to be stuck here.
		 * TODO ask @nikita about how this can be avoided.
		 * temporary solution: use sm group from locality0.
		 */
		rc = m0_be_tx_open_sync(tx);
		M0_ASSERT_INFO(rc == 0,
			       "tx can't fail in recovery mode: rc = %d", rc);
		m0_be_tx_close(tx);
	}
}

M0_INTERNAL int m0_be_tx_group_reconstruct(struct m0_be_tx_group *gr,
					   struct m0_sm_group    *sm_grp)
{
	be_tx_group_reconstruct_reg_area(gr);
	be_tx_group_reconstruct_transactions(gr, sm_grp);
	return 0; /* XXX no error handling yet. It will be fixed. */
}

/*
 * It will perform actual I/O when paged implemented so op is added
 * to the function parameters list.
 */
M0_INTERNAL int m0_be_tx_group_reapply(struct m0_be_tx_group *gr,
				       struct m0_be_op       *op)
{
	struct m0_be_reg_d *rd;

	m0_be_op_active(op);

	M0_BE_REG_AREA_FORALL(&gr->tg_reg_area, rd) {
		memcpy(rd->rd_reg.br_addr, rd->rd_buf, rd->rd_reg.br_size);
	};

	m0_be_op_done(op);
	return 0;
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
