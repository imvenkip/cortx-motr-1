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

#include "be/engine.h"

#include "lib/memory.h"		/* m0_free */
#include "lib/errno.h"		/* ENOMEM */
#include "lib/misc.h"		/* m0_forall */

#include "be/tx_group.h"	/* m0_be_tx_group */

/**
 * @addtogroup be
 *
 * @{
 */

M0_TL_DESCR_DEFINE(etx, "m0_be_engine::eng_txs[]", M0_INTERNAL,
		   struct m0_be_tx, t_engine_linkage, t_magic,
		   M0_BE_TX_MAGIC, M0_BE_TX_ENGINE_MAGIC);

M0_TL_DEFINE(etx, M0_INTERNAL, struct m0_be_tx);

M0_INTERNAL int m0_be_engine_init(struct m0_be_engine *en,
				  struct m0_be_engine_cfg *en_cfg)
{
	int rc;

	*en = (struct m0_be_engine) {
		.eng_cfg	  = en_cfg,
		.eng_group_nr	  = en_cfg->bec_group_nr,
	};

	M0_ASSERT(en_cfg->bec_group_nr == 1);

	M0_ALLOC_ARR(en->eng_group, en_cfg->bec_group_nr);
	if (en->eng_group == NULL) {
		rc = -ENOMEM;
		goto free;
	}

	m0_be_log_init(&en->eng_log, NULL /* XXX */);
	rc = m0_be_log_create(&en->eng_log, en_cfg->bec_log_size);
	if (rc != 0)
		goto log_fini;

	rc = m0_be_tx_group_init(&en->eng_group[0], &en_cfg->bec_group_size_max,
				 en_cfg->bec_group_tx_max,
				 m0_be_log_stob(&en->eng_log),
				 en_cfg->bec_group_fom_reqh);
	if (rc != 0)
		goto log_destroy;
	en->eng_group_closed = false;

	m0_forall(i, ARRAY_SIZE(en->eng_txs),
		  (etx_tlist_init(&en->eng_txs[i]), true));
	m0_mutex_init(&en->eng_lock);
	rc = 0;
	goto out;

	/* left for reference */
	m0_be_tx_group_fini(&en->eng_group[0]);
log_destroy:
	m0_be_log_destroy(&en->eng_log);
log_fini:
	m0_be_log_fini(&en->eng_log);
free:
	m0_free(en->eng_group);
out:
	M0_POST(ergo(rc == 0, m0_be_engine__invariant(en)));
	return rc;
}

M0_INTERNAL void m0_be_engine_fini(struct m0_be_engine *en)
{
	M0_PRE(m0_be_engine__invariant(en));

	m0_mutex_fini(&en->eng_lock);
	m0_forall(i, ARRAY_SIZE(en->eng_txs),
		  (etx_tlist_fini(&en->eng_txs[i]), true));
	m0_be_tx_group_fini(&en->eng_group[0]);
	m0_be_log_destroy(&en->eng_log);
	m0_be_log_fini(&en->eng_log);
	m0_free(en->eng_group);
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
	return true;
}

static void be_engine_got_tx_open(struct m0_be_engine *en)
{
	struct m0_be_tx *tx;
	int              rc;

	M0_PRE(be_engine_is_locked(en));

	m0_tl_for(etx, &en->eng_txs[M0_BTS_OPENING], tx) {
		rc = m0_be_log_reserve_tx(&en->eng_log,
					  &tx->t_prepared);
		if (rc == 0) {
			m0_be_tx__state_post(tx, M0_BTS_ACTIVE);
		} else {
			/* Ignore the rest of OPENING transactions. If we
			 * don't ignore them, the big ones may starve. */
			break;
		}
	} m0_tl_endfor;
}

static void be_engine_group_close(struct m0_be_engine *en,
				  struct m0_be_tx_group *gr)
{
	en->eng_group_closed = true;
	/* TODO */
	/* run group fom */
}

static struct m0_be_tx_group *be_engine_group_find(struct m0_be_engine *en)
{
	return en->eng_group_closed ? NULL : &en->eng_group[0];
}

static int be_engine_tx_trygroup(struct m0_be_engine *en,
				 struct m0_be_tx *tx)
{
	struct m0_be_tx_group *gr;
	int		       rc = -EBUSY;

	M0_PRE(be_engine_is_locked(en));

	while ((gr = be_engine_group_find(en)) != NULL) {
		rc = m0_be_tx_group_add(gr, tx);
		if (rc == 0)
			break;
		be_engine_group_close(en, gr);
	}
	return rc;
}

static void be_engine_got_tx_close(struct m0_be_engine *en)
{
	struct m0_be_tx *tx;
	int              rc;

	M0_PRE(be_engine_is_locked(en));

	m0_tl_for(etx, &en->eng_txs[M0_BTS_CLOSED], tx) {
		rc = be_engine_tx_trygroup(en, tx);
		if (rc != 0)
			break;
		m0_be_tx__state_post(tx, M0_BTS_GROUPED);
	} m0_tl_endfor;
}

static void be_engine_got_log_space(struct m0_be_engine *en)
{
	M0_PRE(be_engine_is_locked(en));

	be_engine_got_tx_open(en);
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
	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	etx_tlink_init_at(tx, &en->eng_txs[state]);

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);
}

M0_INTERNAL void m0_be_engine__tx_state_set(struct m0_be_engine *en,
					    struct m0_be_tx *tx,
					    enum m0_be_tx_state state)
{
	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	etx_tlist_move(&en->eng_txs[state], tx);

	if (state == M0_BTS_OPENING)
		be_engine_got_tx_open(en);
	if (state == M0_BTS_CLOSED)
		be_engine_got_tx_close(en);

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);
}

#if 0
M0_INTERNAL void m0_be_engine__tx_open(struct m0_be_engine *en,
				       struct m0_be_tx *tx)
{
	be_engine_lock(en);
	// rc = m0_be_log_reserve_tx(&tx_engine(tx)->te_log, &tx->t_prepared);
	be_engine_unlock(en);
}

M0_INTERNAL void m0_be_engine__tx_close(struct m0_be_engine *en,
					struct m0_be_tx *tx)
{
	be_engine_lock(en);
	be_engine_unlock(en);
}
#endif

M0_INTERNAL void m0_be_engine__tx_fini(struct m0_be_engine *en,
				       struct m0_be_tx *tx)
{
	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	etx_tlist_del(tx);
	etx_tlink_fini(tx);

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);
}

M0_INTERNAL void m0_be_engine__log_got_space(struct m0_be_engine *en)
{
	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	be_engine_got_log_space(en);

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);
}

/** @} end of be group */

#if 0
M0_INTERNAL void m0_be_tx_engine_init(struct m0_be_tx_engine *engine)
{
	int rc;

	m0_be_log_init(&engine->te_log);
	rc = m0_be_log_create(&engine->te_log, 1ULL << 28);
	M0_ASSERT(rc == 0); /* XXX FIXME */
	m0_forall(i, ARRAY_SIZE(engine->te_txs),
		  (eng_tlist_init(&engine->te_txs[i]), true));
	m0_rwlock_init(&engine->te_lock);
	tx_group_init(&engine->te_group, m0_be_log_stob(&engine->te_log));

	M0_POST(m0_be__tx_engine_invariant(engine));
}

M0_INTERNAL void m0_be_tx_engine_fini(struct m0_be_tx_engine *engine)
{
	M0_PRE(m0_be__tx_engine_invariant(engine));

	tx_group_fini(&engine->te_group);
	m0_rwlock_fini(&engine->te_lock);
	m0_forall(i, ARRAY_SIZE(engine->te_txs),
		  (eng_tlist_fini(&engine->te_txs[i]), true));
	m0_be_log_destroy(&engine->te_log);
	m0_be_log_fini(&engine->te_log);
}
#endif

#if 0
static void tx_engine_got_space(struct m0_be_tx_engine *eng)
{
	M0_PRE(m0_be__tx_engine_invariant(eng));


	M0_POST(m0_be__tx_engine_invariant(eng));
}
#endif

#if 0
M0_INTERNAL bool
m0_be__tx_engine_invariant(const struct m0_be_tx_engine *engine)
{
	struct m0_be_tx *prev = NULL;

	return true || /* XXX RESTOREME */
		true;
		(m0_forall(i, M0_BTS_NR,
			   m0_tl_forall(eng, t, &engine->te_txs[i],
					m0_be_tx__invariant(t) &&
					ergo(prev != NULL && prev->t_lsn != 0,
					     t->t_lsn != 0 &&
					     prev->t_lsn > t->t_lsn) &&
					(prev = t, true))) &&
		 /* tx_engine doesn't keep failed transactions in its lists. */
		 eng_tlist_is_empty(&engine->te_txs[M0_BTS_FAILED]));
}
#endif

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
