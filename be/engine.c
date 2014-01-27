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
 * Original creation date: 17-Jul-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/engine.h"

#include "lib/memory.h"		/* m0_free */
#include "lib/errno.h"		/* ENOMEM */
#include "lib/misc.h"		/* m0_forall */

#include "be/tx_group.h"	/* m0_be_tx_group */
#include "be/tx_internal.h"	/* m0_be_tx__state_post */

/**
 * @addtogroup be
 *
 * @{
 */

M0_TL_DESCR_DEFINE(etx, "m0_be_engine::eng_txs[]", M0_INTERNAL,
		   struct m0_be_tx, t_engine_linkage, t_magic,
		   M0_BE_TX_MAGIC, M0_BE_TX_ENGINE_MAGIC);
M0_TL_DEFINE(etx, M0_INTERNAL, struct m0_be_tx);

M0_TL_DESCR_DEFINE(egr, "m0_be_engine::eng_groups[]", static,
		   struct m0_be_tx_group, tg_engine_linkage, tg_magic,
		   M0_BE_TX_MAGIC /* XXX */, M0_BE_TX_ENGINE_MAGIC /* XXX */);
M0_TL_DEFINE(egr, static, struct m0_be_tx_group);

static void be_engine_got_log_space_cb(struct m0_be_log *log);

M0_INTERNAL int
m0_be_engine_init(struct m0_be_engine *en, struct m0_be_engine_cfg *en_cfg)
{
	int rc;
	int i;

	M0_ENTRY();

	*en = (struct m0_be_engine) {
		.eng_cfg      = en_cfg,
		.eng_group_nr = en_cfg->bec_group_nr,
	};

	M0_ASSERT_INFO(m0_be_tx_credit_le(&en_cfg->bec_tx_size_max,
					  &en_cfg->bec_group_size_max),
		       "Maximum transaction size shouldn't be greater than "
		       "maximum group size: "
		       "tx_size_max = " BETXCR_F ", group_size_max = " BETXCR_F,
		       BETXCR_P(&en_cfg->bec_tx_size_max),
		       BETXCR_P(&en_cfg->bec_group_size_max));
	M0_ASSERT_INFO(en_cfg->bec_log_size >=
		       en_cfg->bec_group_size_max.tc_reg_size,
		       "Log size shouldn't be less than maximum group size: "
		       "log_size = %lu, group_size_max = %lu",
		       en_cfg->bec_log_size,
		       en_cfg->bec_group_size_max.tc_reg_size);
	M0_ASSERT_INFO(en_cfg->bec_group_nr == 1,
		       "Only one group is supported at the moment, "
		       "but group_nr = %zu", en_cfg->bec_group_nr);
	M0_ASSERT_INFO(!en_cfg->bec_log_replay, "Recovery is not implemented");

	M0_ALLOC_ARR(en->eng_group, en_cfg->bec_group_nr);
	if (en->eng_group == NULL) {
		rc = -ENOMEM;
		goto err;
	}

	m0_be_log_init(&en->eng_log, en_cfg->bec_log_stob,
		       be_engine_got_log_space_cb);
	rc = m0_be_log_create(&en->eng_log, en_cfg->bec_log_size);
	if (rc != 0)
		goto log_fini;

	m0_forall(i, ARRAY_SIZE(en->eng_groups),
		  (egr_tlist_init(&en->eng_groups[i]), true));
	for (i = 0; i < en->eng_group_nr; ++i) {
		rc = m0_be_tx_group_init(&en->eng_group[0],
					 &en_cfg->bec_group_size_max,
					 en_cfg->bec_group_tx_max,
					 en,
					 &en->eng_log,
					 en_cfg->bec_group_fom_reqh);
		/* XXX invalid for number of groups > 1 */
		if (rc != 0)
			goto log_destroy;
		egr_tlink_init(&en->eng_group[0]);
	}
	en->eng_group_closed = false;

	m0_forall(i, ARRAY_SIZE(en->eng_txs),
		  (etx_tlist_init(&en->eng_txs[i]), true));
	m0_mutex_init(&en->eng_lock);

	M0_ASSERT(rc == 0);
	M0_POST(m0_be_engine__invariant(en));
	M0_RETURN(0);

log_destroy:
	m0_forall(i, ARRAY_SIZE(en->eng_groups),
		  (egr_tlist_init(&en->eng_groups[i]), true));
	m0_be_log_destroy(&en->eng_log);
log_fini:
	m0_be_log_fini(&en->eng_log);
	m0_free(en->eng_group);
err:
	M0_ASSERT(rc != 0);
	M0_RETURN(rc);
}

M0_INTERNAL void m0_be_engine_fini(struct m0_be_engine *en)
{
	m0_bcount_t log_size = m0_be_log_size(&en->eng_log);
	m0_bcount_t log_free = m0_be_log_free(&en->eng_log);
	int	    i;

	M0_ENTRY();
	M0_PRE(m0_be_engine__invariant(en));

	M0_ASSERT_INFO(log_size == log_free,
		       "There is at least one transaction which didn't become "
		       "stable yet. log_size = %lu, log_free = %lu",
		       log_size, log_free);

	m0_mutex_fini(&en->eng_lock);
	m0_forall(i, ARRAY_SIZE(en->eng_txs),
		  (etx_tlist_fini(&en->eng_txs[i]), true));
	for (i = 0; i < en->eng_group_nr; ++i) {
		m0_be_tx_group_fini(&en->eng_group[i]);
		egr_tlink_fini(&en->eng_group[i]);
	}
	m0_forall(i, ARRAY_SIZE(en->eng_groups),
		  (egr_tlist_fini(&en->eng_groups[i]), true));
	m0_be_log_destroy(&en->eng_log);
	m0_be_log_fini(&en->eng_log);
	m0_free(en->eng_group);

	M0_LEAVE();
}

static void be_engine_lock(struct m0_be_engine *en)
{
	m0_mutex_lock(&en->eng_lock);
}

static void be_engine_unlock(struct m0_be_engine *en)
{
	m0_mutex_unlock(&en->eng_lock);
}

static bool be_engine_is_locked(struct m0_be_engine *en)
{
	return m0_mutex_is_locked(&en->eng_lock);
}

static bool be_engine_invariant(struct m0_be_engine *en)
{
	M0_PRE(be_engine_is_locked(en));
	return true; /* XXX TODO */
}

static struct m0_be_tx *be_engine_tx_peek(struct m0_be_engine *en,
					  enum m0_be_tx_state state)
{
	M0_PRE(be_engine_is_locked(en));

	return etx_tlist_head(&en->eng_txs[state]);
}

static void be_engine_tx_state_post(struct m0_be_engine *en,
				    struct m0_be_tx *tx,
				    enum m0_be_tx_state state)
{
	M0_PRE(be_engine_is_locked(en));

	etx_tlist_move(&en->eng_txs[M0_BTS_NR], tx);
	m0_be_tx__state_post(tx, state);
}

/* XXX RENAME IT */
static void be_engine_got_tx_open(struct m0_be_engine *en)
{
	struct m0_be_tx *tx;
	int              rc;

	M0_PRE(be_engine_is_locked(en));

	while ((tx = be_engine_tx_peek(en, M0_BTS_OPENING)) != NULL) {
		if (!m0_be_tx_credit_le(&tx->t_prepared,
					&en->eng_cfg->bec_tx_size_max)) {
			M0_LOG(M0_DEBUG, "tx %p, engine %p: size of prepared "
			       "credit "BETXCR_F" exceeded maximum tx size "
			       BETXCR_F, tx, en, BETXCR_P(&tx->t_prepared),
			       BETXCR_P(&en->eng_cfg->bec_tx_size_max));
			be_engine_tx_state_post(en, tx, M0_BTS_FAILED);
		} else {
			rc = m0_be_log_reserve_tx(&en->eng_log,
						  &tx->t_prepared);
			if (rc == 0) {
				tx->t_log_reserved = true;
				be_engine_tx_state_post(en, tx, M0_BTS_ACTIVE);
			} else {
				/* Ignore the rest of OPENING transactions.
				 * If we don't ignore them, the big ones
				 * may starve.
				 */
				break;
			}
		}
	}
}

static void be_engine_group_close(struct m0_be_engine *en,
				  struct m0_be_tx_group *gr,
				  bool immediately)
{
	m0_time_t abs_timeout;

	M0_PRE(be_engine_is_locked(en));

	if (immediately) {
		abs_timeout = M0_TIME_IMMEDIATELY;
		egr_tlist_move(&en->eng_groups[M0_BEG_CLOSED], gr);
	} else {
		abs_timeout = m0_time_now() +
			      en->eng_cfg->bec_group_close_timeout;
	}
	m0_be_tx_group_close(gr, abs_timeout);
}

static struct m0_be_tx_group *be_engine_group_find(struct m0_be_engine *en)
{
	return egr_tlist_head(&en->eng_groups[M0_BEG_OPEN]);
}

static int be_engine_tx_trygroup(struct m0_be_engine *en,
				 struct m0_be_tx *tx)
{
	struct m0_be_tx_group *gr;
	int		       rc = -EBUSY;

	M0_PRE(be_engine_is_locked(en));

	while ((gr = be_engine_group_find(en)) != NULL) {
		rc = m0_be_tx_group_tx_add(gr, tx);
		if (rc == 0) {
			tx->t_group = gr;
			if (m0_be_tx__is_fast(tx))
				be_engine_group_close(en, gr, true);
			else if (m0_be_tx_group_size(gr) == 1)
				be_engine_group_close(en, gr, false);
			break;
		}
		be_engine_group_close(en, gr, true);
	}
	return rc;
}

/* XXX RENAME IT */
static void be_engine_got_tx_close(struct m0_be_engine *en)
{
	struct m0_be_tx *tx;
	int              rc;

	M0_PRE(be_engine_is_locked(en));

	while ((tx = be_engine_tx_peek(en, M0_BTS_CLOSED)) != NULL) {
		rc = be_engine_tx_trygroup(en, tx);
		if (rc != 0)
			break;
		be_engine_tx_state_post(en, tx, M0_BTS_GROUPED);
	}
}

static void be_engine_got_tx_done(struct m0_be_engine *en, struct m0_be_tx *tx)
{
	struct m0_be_tx_group *gr = tx->t_group;

	M0_PRE(be_engine_is_locked(en));

	M0_CNT_DEC(gr->tg_nr_unstable);
	if (gr->tg_nr_unstable == 0)
		m0_be_tx_group_stable(gr);
	tx->t_group = NULL;
}

M0_INTERNAL bool m0_be_engine__invariant(struct m0_be_engine *en)
{
	bool rc_bool;

	be_engine_lock(en);
	rc_bool = be_engine_invariant(en);
	be_engine_unlock(en);

	return rc_bool;
}

M0_INTERNAL void m0_be_engine__tx_init(struct m0_be_engine *en,
				       struct m0_be_tx *tx,
				       enum m0_be_tx_state state)
{
	etx_tlink_init(tx);
	m0_be_engine__tx_state_set(en, tx, state);
}

M0_INTERNAL void m0_be_engine__tx_fini(struct m0_be_engine *en,
				       struct m0_be_tx *tx)
{
	etx_tlink_fini(tx);
}

M0_INTERNAL void m0_be_engine__tx_state_set(struct m0_be_engine *en,
					    struct m0_be_tx *tx,
					    enum m0_be_tx_state state)
{
	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	M0_LOG(M0_DEBUG, "tx %p: => %s", tx, m0_be_tx_state_name(state));

	if (state != M0_BTS_PREPARE)
		etx_tlist_del(tx);
	if (!M0_IN(state, (M0_BTS_DONE, M0_BTS_FAILED)))
		etx_tlist_add_tail(&en->eng_txs[state], tx);

	switch (state) {
	case M0_BTS_OPENING:
		be_engine_got_tx_open(en);
		break;
	case M0_BTS_CLOSED:
		be_engine_got_tx_close(en);
		break;
	case M0_BTS_DONE:
		be_engine_got_tx_done(en, tx);
		break;
	case M0_BTS_FAILED:
		if (tx->t_log_reserved)
			m0_be_log_discard(&en->eng_log, &tx->t_prepared);
		break;
	default:
		break;
	}

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);
}

M0_INTERNAL void m0_be_engine__tx_group_open(struct m0_be_engine *en,
					     struct m0_be_tx_group *gr)
{
	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	/* TODO check if group is in M0_BEG_CLOSED list */
	egr_tlist_move(&en->eng_groups[M0_BEG_OPEN], gr);
	be_engine_got_tx_close(en);

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);
}

M0_INTERNAL void m0_be_engine__tx_group_close(struct m0_be_engine *en,
					      struct m0_be_tx_group *gr)
{
	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	/*
	 * tx_group may be already in the list M0_BEG_CLOSED, move() should
	 * handle this situation correctly
	 */
	egr_tlist_move(&en->eng_groups[M0_BEG_CLOSED], gr);

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);
}

static void be_engine_group_stop_nr(struct m0_be_engine *en, size_t nr)
{
	M0_PRE(be_engine_is_locked(en));

	size_t i;

	for (i = 0; i < nr; ++i) {
		/*
		 * XXX engine lock-unlock is temporary solution
		 * to prevent deadlock.
		 */
		be_engine_unlock(en);
		m0_be_tx_group_stop(&en->eng_group[i]);
		be_engine_lock(en);
		egr_tlist_del(&en->eng_group[i]);
	}
}

M0_INTERNAL void m0_be_engine_start(struct m0_be_engine *en)
{
	size_t i;

	M0_ENTRY();
	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	for (i = 0; i < en->eng_group_nr; ++i) {
		m0_be_tx_group_start(&en->eng_group[i]);
		egr_tlist_add_tail(&en->eng_groups[M0_BEG_OPEN],
				   &en->eng_group[i]);
	}
	if (i != en->eng_group_nr)
		be_engine_group_stop_nr(en, i);

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);
	M0_LEAVE();
}

M0_INTERNAL void m0_be_engine_stop(struct m0_be_engine *en)
{
	M0_ENTRY();
	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	be_engine_group_stop_nr(en, en->eng_group_nr);

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);
	M0_LEAVE();
}

static void be_engine_got_log_space_cb(struct m0_be_log *log)
{
	struct m0_be_engine *en =
		container_of(log, struct m0_be_engine, eng_log);

	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	be_engine_got_tx_open(en);

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);
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
