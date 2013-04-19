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
 * Original creation date: 27-Jan-2013
 */


/**
 * @addtogroup dtm
 *
 * @{
 */

#include "lib/assert.h"

#include "dtm/update.h"
#include "dtm/ltx.h"

static void ltx_persistent_hook(struct m0_db_tx_waiter *w);
static void ltx_noop(struct m0_db_tx_waiter *w);
static void ltx_abort(struct m0_db_tx_waiter *w);
static const struct m0_dtm_history_ops ltx_ops;

M0_INTERNAL void m0_dtm_ltx_init(struct m0_dtm_ltx *ltx, struct m0_dtm *dtm,
				 struct m0_dbenv *env)
{
	m0_dtm_controlh_init(&ltx->lx_ch, dtm);
	ltx->lx_ch.ch_history.h_ops = &ltx_ops;
	ltx->lx_ch.ch_history.h_hi.hi_flags |= M0_DHF_OWNED;
	ltx->lx_ch.ch_history.h_dtm = NULL;
	ltx->lx_env = env;
}

M0_INTERNAL int m0_dtm_ltx_open(struct m0_dtm_ltx *ltx)
{
	int result;

	result = m0_db_tx_init(&ltx->lx_tx, ltx->lx_env, 0);
	if (result == 0) {
		ltx->lx_waiter.tw_persistent = &ltx_persistent_hook;
		ltx->lx_waiter.tw_commit     = &ltx_noop;
		ltx->lx_waiter.tw_done       = &ltx_noop;
		ltx->lx_waiter.tw_abort      = &ltx_abort;
		m0_db_tx_waiter_add(&ltx->lx_tx, &ltx->lx_waiter);
	}
	return result;
}

M0_INTERNAL int m0_dtm_ltx_close(struct m0_dtm_ltx *ltx)
{
	m0_dtm_controlh_close(&ltx->lx_ch);
	m0_dtm_oper_prepared(&ltx->lx_ch.ch_clop);
	m0_dtm_oper_done(&ltx->lx_ch.ch_clop, NULL);
	return m0_db_tx_commit(&ltx->lx_tx);
}

M0_INTERNAL void m0_dtm_ltx_fini(struct m0_dtm_ltx *ltx)
{
	m0_dtm_controlh_fini(&ltx->lx_ch);
}

M0_INTERNAL void m0_dtm_ltx_add(struct m0_dtm_ltx *ltx,
				struct m0_dtm_oper *oper)
{
	m0_dtm_controlh_add(&ltx->lx_ch, oper);
}

static void ltx_persistent_hook(struct m0_db_tx_waiter *w)
{
	struct m0_dtm_ltx *ltx = container_of(w, struct m0_dtm_ltx, lx_waiter);
	M0_ASSERT(ltx->lx_ch.ch_history.h_hi.hi_flags & M0_DHF_CLOSED);
	m0_dtm_history_persistent(&ltx->lx_ch.ch_history, ~0ULL);
}

static void ltx_noop(struct m0_db_tx_waiter *w)
{}

static void ltx_abort(struct m0_db_tx_waiter *w)
{
	M0_IMPOSSIBLE("Aborting ltx?");
}

static int ltx_find(struct m0_dtm *dtm, const struct m0_dtm_history_type *ht,
		    const struct m0_uint128 *id,
		    struct m0_dtm_history **out)
{
	M0_IMPOSSIBLE("Looking for ltx?");
}

static const struct m0_dtm_history_type_ops ltx_htype_ops = {
	.hito_find = ltx_find
};

enum {
	M0_DTM_HTYPE_LTX = 7
};

M0_INTERNAL const struct m0_dtm_history_type m0_dtm_ltx_htype = {
	.hit_id   = M0_DTM_HTYPE_LTX,
	.hit_name = "local transaction",
	.hit_ops  = &ltx_htype_ops
};

static const struct m0_uint128 *ltx_id(const struct m0_dtm_history *history)
{
	M0_IMPOSSIBLE("Encoding ltx?");
	return NULL;
}

static const struct m0_dtm_history_ops ltx_ops = {
	.hio_type       = &m0_dtm_ltx_htype,
	.hio_id         = &ltx_id,
	.hio_persistent = (void *)&ltx_noop,
	.hio_fixed      = (void *)&ltx_noop,
	.hio_update     = &m0_dtm_controlh_update
};


/** @} end of dtm group */

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
