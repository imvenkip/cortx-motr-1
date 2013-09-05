/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 10/11/2012
 */

/* This file is designed to be included by addb/addb.c */

/**
   @ingroup addb_pvt
   @{
 */

static bool addb_mc_invariant(const struct m0_addb_mc *mc)
{
	return mc->am_magic == M0_ADDB_MC_MAGIC &&
		ergo(mc->am_evmgr != NULL,
		     mc->am_evmgr->evm_get != NULL &&
		     mc->am_evmgr->evm_put != NULL &&
		     mc->am_evmgr->evm_rec_alloc != NULL &&
		     mc->am_evmgr->evm_post != NULL) &&
		ergo(mc->am_evmgr != NULL && mc->am_evmgr->evm_post_awkward,
		     mc->am_evmgr->evm_copy != NULL) &&
		ergo(mc->am_sink != NULL,
		     mc->am_sink->rs_get != NULL &&
		     mc->am_sink->rs_put != NULL &&
		     mc->am_sink->rs_rec_alloc != NULL &&
		     mc->am_sink->rs_save != NULL);
}

/** @} end group addb_pvt */

/* Public interfaces */

struct m0_addb_mc m0_addb_gmc;
M0_EXPORTED(m0_addb_gmc);

M0_INTERNAL void m0_addb_mc_init(struct m0_addb_mc *mc)
{
	mc->am_evmgr = NULL;
	mc->am_sink  = NULL;
	mc->am_magic = M0_ADDB_MC_MAGIC;
}

M0_INTERNAL bool m0_addb_mc_is_initialized(const struct m0_addb_mc *mc)
{
	return addb_mc_invariant(mc);
}

M0_INTERNAL void m0_addb_mc_unconfigure(struct m0_addb_mc *mc)
{
	M0_PRE(m0_addb_mc_is_initialized(mc));
	if (mc->am_evmgr != NULL) {
		struct m0_addb_mc_evmgr *mgr = mc->am_evmgr;

		mc->am_evmgr = NULL;
		(*mgr->evm_put)(mc, mgr);
	}
	if (mc->am_sink != NULL) {
		struct m0_addb_mc_recsink *sink = mc->am_sink;

		mc->am_sink = NULL;
		M0_LOG(M0_DEBUG, "put sink %p", sink);
		(*sink->rs_put)(mc, sink);
	}
}

M0_INTERNAL void m0_addb_mc_fini(struct m0_addb_mc *mc)
{
	m0_addb_mc_unconfigure(mc);
	mc->am_magic = 0;
	M0_POST(!m0_addb_mc_is_initialized(mc));
}

M0_INTERNAL bool m0_addb_mc_has_evmgr(const struct m0_addb_mc *mc)
{
	M0_PRE(m0_addb_mc_is_initialized(mc));
	return mc->am_evmgr != NULL;
}

M0_INTERNAL bool m0_addb_mc_has_recsink(const struct m0_addb_mc *mc)
{
	M0_PRE(m0_addb_mc_is_initialized(mc));
	return mc->am_sink != NULL;
}

M0_INTERNAL bool m0_addb_mc_is_configured(const struct m0_addb_mc *mc)
{
	return m0_addb_mc_has_evmgr(mc) || m0_addb_mc_has_recsink(mc);
}

M0_INTERNAL bool m0_addb_mc_is_fully_configured(const struct m0_addb_mc *mc)
{
	return m0_addb_mc_has_evmgr(mc) && m0_addb_mc_has_recsink(mc);
}

M0_INTERNAL bool m0_addb_mc_can_post_awkward(const struct m0_addb_mc *mc)
{
	return m0_addb_mc_has_evmgr(mc) && mc->am_evmgr->evm_post_awkward;
}

M0_INTERNAL void m0_addb_mc_dup(const struct m0_addb_mc *src_mc,
				struct m0_addb_mc *tgt_mc)
{
	M0_PRE(m0_addb_mc_is_configured(src_mc));
	M0_PRE(!m0_addb_mc_can_post_awkward(src_mc));
	M0_PRE(!m0_addb_mc_is_configured(tgt_mc));
	*tgt_mc = *src_mc;
	if (m0_addb_mc_has_evmgr(src_mc))
		(*tgt_mc->am_evmgr->evm_get)(tgt_mc, tgt_mc->am_evmgr);
	if (m0_addb_mc_has_recsink(src_mc)) {
		(*tgt_mc->am_sink->rs_get)(tgt_mc, tgt_mc->am_sink);
		if (tgt_mc == &m0_addb_gmc)
			addb_ctx_global_post();
	}
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
