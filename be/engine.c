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

#include "lib/memory.h"         /* m0_free */
#include "lib/errno.h"          /* ENOMEM */
#include "lib/misc.h"           /* m0_forall */
#include "lib/time.h"           /* m0_time_now */

#include "be/tx_service.h"      /* m0_be_tx_service_init */
#include "be/tx_group.h"        /* m0_be_tx_group */
#include "be/tx_internal.h"     /* m0_be_tx__state_post */

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

/* engine first capture */
M0_TL_DESCR_DEFINE(efc, "m0_be_engine::eng_tx_first_capture[]", M0_INTERNAL,
		   struct m0_be_tx, t_first_capture_linkage, t_magic,
		   M0_BE_TX_MAGIC, M0_BE_TX_ENGINE_MAGIC);
M0_TL_DEFINE(efc, M0_INTERNAL, struct m0_be_tx);

static bool be_engine_is_locked(const struct m0_be_engine *en);
static void be_engine_group_freeze(struct m0_be_engine   *en,
                                   struct m0_be_tx_group *gr);
static void be_engine_group_tryclose(struct m0_be_engine   *en,
                                     struct m0_be_tx_group *gr);

static void be_engine_tx_group_state_move(struct m0_be_engine       *en,
                                          struct m0_be_tx_group     *gr,
                                          enum m0_be_tx_group_state  state)
{
	static const enum m0_be_tx_group_state prev_state[] = {
		[M0_BGS_OPEN]   = M0_BGS_CLOSED,
		[M0_BGS_FROZEN] = M0_BGS_OPEN,
		[M0_BGS_CLOSED] = M0_BGS_FROZEN,
	};
	M0_ENTRY("en=%p gr=%p state=%d gr->tg_state=%d",
		 en, gr, state, gr->tg_state);

	M0_PRE(be_engine_is_locked(en));
	M0_PRE(gr->tg_state == prev_state[state]);
	M0_PRE(m0_tl_exists(egr, gr_in_list, &en->eng_groups[gr->tg_state],
	                    gr_in_list == gr));

	egr_tlist_move(&en->eng_groups[state], gr);
	gr->tg_state = state;
}

static int be_engine_cfg_validate(struct m0_be_engine_cfg *en_cfg)
{
	M0_ASSERT_INFO(m0_be_tx_credit_le(&en_cfg->bec_tx_size_max,
					  &en_cfg->bec_group_cfg.tgc_size_max),
		       "Maximum transaction size shouldn't be greater than "
		       "maximum group size: "
		       "tx_size_max = " BETXCR_F ", group_size_max = " BETXCR_F,
		       BETXCR_P(&en_cfg->bec_tx_size_max),
		       BETXCR_P(&en_cfg->bec_group_cfg.tgc_size_max));
	M0_ASSERT(en_cfg->bec_tx_payload_max <=
		  en_cfg->bec_group_cfg.tgc_payload_max);
	M0_ASSERT_INFO(en_cfg->bec_group_nr == 1,
		       "Only one group is supported at the moment, "
		       "but group_nr = %zu", en_cfg->bec_group_nr);
	return 0;
}

M0_INTERNAL int m0_be_engine_init(struct m0_be_engine     *en,
				  struct m0_be_domain     *dom,
				  struct m0_be_engine_cfg *en_cfg)
{
	struct m0_be_tx_group_cfg *gr_cfg;
	struct m0_be_tx_group	  *gr;
	int			   rc;
	int			   i;

	M0_ENTRY();

	rc = be_engine_cfg_validate(en_cfg);
	M0_ASSERT(rc == 0);

	en->eng_cfg      = en_cfg;
	en->eng_group_nr = en_cfg->bec_group_nr;
	en->eng_domain   = dom;
	en->eng_recovery = en_cfg->bec_recovery;

	M0_ALLOC_ARR(en->eng_group, en_cfg->bec_group_nr);
	if (en->eng_group == NULL) {
		rc = -ENOMEM;
		goto err;
	}
	M0_ALLOC_ARR(en_cfg->bec_groups_cfg, en_cfg->bec_group_nr);
	M0_ASSERT(en_cfg->bec_groups_cfg != NULL);

	m0_forall(i, ARRAY_SIZE(en->eng_groups),
		  (egr_tlist_init(&en->eng_groups[i]), true));
	rc = m0_be_tx_service_init(en, en_cfg->bec_reqh);
	if (rc != 0)
		goto err_free;
	/**
	 * @todo Recovery is disabled because of bug MERO-1109.
	 * Once bug is fixed, enable it again.
	 */
	/*rc = m0_be_recovery_run(en->eng_recovery, &en->eng_log);*/
	rc = 0;
	if (rc != 0)
		goto err_service_fini;
	for (i = 0; i < en->eng_group_nr; ++i) {
		gr	= &en->eng_group[i];
		gr_cfg  = &en_cfg->bec_groups_cfg[i];

		*gr_cfg		   = en_cfg->bec_group_cfg;
		gr_cfg->tgc_domain = en_cfg->bec_domain;
		gr_cfg->tgc_engine = en;
		gr_cfg->tgc_log	   = &en->eng_log;
		gr_cfg->tgc_reqh   = en_cfg->bec_reqh;

		rc = m0_be_tx_group_init(gr, gr_cfg);
		M0_ASSERT(rc == 0);
		m0_sm_timer_init(&gr->tg_close_timer);
		egr_tlink_init(gr);
	}
	en->eng_tx_id_next = 0;

	m0_forall(i, ARRAY_SIZE(en->eng_txs),
		  (etx_tlist_init(&en->eng_txs[i]), true));
	m0_mutex_init(&en->eng_lock);

	m0_mutex_init(&en->eng_reg_area_lock);
	for (i = 0; i < ARRAY_SIZE(en->eng_reg_area); ++i) {
		en_cfg->bec_reg_area_size_max.tc_reg_nr *= 2;
		rc = m0_be_reg_area_init(&en->eng_reg_area[i],
					 &en_cfg->bec_reg_area_size_max,
					 M0_BE_REG_AREA_DATA_NOCOPY);
		/*
		 * Assertion is enough here before m0_module is used
		 * for error handling.
		 */
		M0_ASSERT_INFO(rc == 0, "rc = %d", rc); /* XXX */
	}
	en->eng_reg_area_index = 0;
	en->eng_reg_area_prune_gen_idx_min = ULONG_MAX;
	efc_tlist_init(&en->eng_tx_first_capture);

	m0_semaphore_init(&en->eng_recovery_wait_sem, 0);
	en->eng_recovery_finished = false;

	M0_POST(m0_be_engine__invariant(en));
	return M0_RC(0);
 err_service_fini:
	m0_be_tx_service_fini(en);
 err_free:
	m0_free(en->eng_group);
 err:
	return M0_RC(rc);
}

M0_INTERNAL void m0_be_engine_fini(struct m0_be_engine *en)
{
	struct m0_be_tx_credit used;
	m0_bcount_t log_size = 0; /* XXX */
	m0_bcount_t log_free = 0; /* XXX */
	int         i;

	M0_ENTRY();
	M0_PRE(m0_be_engine__invariant(en));

	/*
	 * TODO this check should be implemented.
	 */
	M0_ASSERT_INFO(log_size == log_free,
		       "There is at least one transaction which didn't become "
		       "stable yet. log_size = %lu, log_free = %lu",
		       log_size, log_free);

	m0_semaphore_fini(&en->eng_recovery_wait_sem);

	m0_be_engine__reg_area_lock(en);
	m0_be_engine__reg_area_prune(en);
	m0_be_engine__reg_area_unlock(en);

	efc_tlist_fini(&en->eng_tx_first_capture);
	m0_be_reg_area_used(&en->eng_reg_area[en->eng_reg_area_index], &used);
	M0_PRE(m0_be_tx_credit_eq(&used, &M0_BE_TX_CREDIT(0, 0)));
	for (i = 0; i < ARRAY_SIZE(en->eng_reg_area); ++i)
	     m0_be_reg_area_fini(&en->eng_reg_area[i]);
	m0_mutex_fini(&en->eng_reg_area_lock);
	m0_mutex_fini(&en->eng_lock);
	m0_forall(i, ARRAY_SIZE(en->eng_txs),
		  (etx_tlist_fini(&en->eng_txs[i]), true));
	for (i = 0; i < en->eng_group_nr; ++i) {
		egr_tlink_fini(&en->eng_group[i]);
		m0_sm_timer_fini(&en->eng_group[i].tg_close_timer);
		m0_be_tx_group_fini(&en->eng_group[i]);
	}
	m0_be_tx_service_fini(en);
	m0_forall(i, ARRAY_SIZE(en->eng_groups),
		  (egr_tlist_fini(&en->eng_groups[i]), true));
	m0_free(en->eng_group);
	m0_free(en->eng_cfg->bec_groups_cfg);

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

static bool be_engine_is_locked(const struct m0_be_engine *en)
{
	return m0_mutex_is_locked(&en->eng_lock);
}

static bool be_engine_invariant(struct m0_be_engine *en)
{
	M0_PRE(be_engine_is_locked(en));
	return true; /* XXX TODO */
}

static uint64_t be_engine_tx_id_allocate(struct m0_be_engine *en)
{
	return en->eng_tx_id_next++;
}

static struct m0_be_tx *be_engine_tx_peek(struct m0_be_engine *en,
					  enum m0_be_tx_state  state)
{
	M0_PRE(be_engine_is_locked(en));

	return etx_tlist_head(&en->eng_txs[state]);
}

static void be_engine_tx_state_post(struct m0_be_engine *en,
				    struct m0_be_tx     *tx,
				    enum m0_be_tx_state  state)
{
	M0_PRE(be_engine_is_locked(en));

	etx_tlist_move(&en->eng_txs[M0_BTS_NR], tx);
	m0_be_tx__state_post(tx, state);
}

/**
 * If @stopped engine is stopped for new exclusive transaction, otherwize
 * exclusive transaction is at M0_BTS_DONE state.
 */
static bool is_engine_at_stopped_or_done(const struct m0_be_engine *en,
					 bool                       stopped)
{
	M0_PRE(be_engine_is_locked(en));

	return m0_forall(state, M0_BTS_DONE,
			 state < M0_BTS_ACTIVE ? true :
			 etx_tlist_is_empty(&en->eng_txs[state])) &&
		!m0_tl_exists(etx, tx, &en->eng_txs[M0_BTS_NR],
			      (stopped ? M0_BTS_OPENING : M0_BTS_ACTIVE)
			      <= m0_be_tx_state(tx) &&
			      m0_be_tx_state(tx) <= M0_BTS_DONE);
}

static struct m0_be_tx *be_engine_tx_opening_peek(struct m0_be_engine *en)
{
	struct m0_be_tx *tx;

	M0_PRE(be_engine_is_locked(en));

	if (en->eng_exclusive_mode)
		return NULL;

	tx = be_engine_tx_peek(en, M0_BTS_OPENING);
	if (tx != NULL && m0_be_tx__is_exclusive(tx)) {
		en->eng_exclusive_mode = is_engine_at_stopped_or_done(en, true);
		if (!en->eng_exclusive_mode)
			return NULL;
	}

	return tx;
}

/* XXX RENAME IT */
static void be_engine_got_tx_open(struct m0_be_engine *en,
				  struct m0_be_tx     *tx)
{
	int rc;

	M0_PRE(be_engine_is_locked(en));

	/* XXX s/tx->t_group/tx_is_recovering */
	if (tx != NULL && tx->t_group != NULL) {
		/* XXX this is copypaste */
		tx->t_log_reserved = true;
		be_engine_tx_state_post(en, tx, M0_BTS_GROUPING);
	}

	while ((tx = be_engine_tx_opening_peek(en)) != NULL) {
		if (!m0_be_tx_credit_le(&tx->t_prepared,
					&en->eng_cfg->bec_tx_size_max) ||
		    tx->t_payload.b_nob > en->eng_cfg->bec_tx_payload_max) {
			M0_LOG(M0_ERROR,
			       "tx=%p engine=%p t_prepared="BETXCR_F" "
			       "t_payload.b_nob=%lu bec_tx_size_max="BETXCR_F" "
			       "bec_tx_payload_max=%lu",
			       tx, en, BETXCR_P(&tx->t_prepared),
			       tx->t_payload.b_nob,
			       BETXCR_P(&en->eng_cfg->bec_tx_size_max),
			       en->eng_cfg->bec_tx_payload_max);
			be_engine_tx_state_post(en, tx, M0_BTS_FAILED);
		} else {
			tx->t_log_reserved_size =
				m0_be_group_format_log_reserved_size(
					&en->eng_log, &tx->t_prepared,
					tx->t_payload.b_nob);
			rc = m0_be_log_reserve(&en->eng_log,
					       tx->t_log_reserved_size);
			if (rc == 0) {
				tx->t_log_reserved = true;
				be_engine_tx_state_post(en, tx, M0_BTS_GROUPING);
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

static void be_engine_group_timer_cb(struct m0_sm_timer *timer)
{
	struct m0_be_tx_group *gr     = M0_AMB(gr, timer, tg_close_timer);
	struct m0_be_engine   *en     = gr->tg_engine;
	struct m0_sm_group    *sm_grp = timer->tr_grp;

	M0_ENTRY("en=%p gr=%p sm_grp=%p", en, gr, sm_grp);
	m0_sm_ast_cancel(sm_grp, &gr->tg_close_timer_disarm);

	be_engine_lock(en);
	be_engine_group_freeze(en, gr);
	be_engine_group_tryclose(en, gr);
	be_engine_unlock(en);
	M0_LEAVE();
}

static void be_engine_group_timer_arm(struct m0_sm_group *sm_grp,
                                      struct m0_sm_ast   *ast)
{
	struct m0_be_tx_group *gr    = M0_AMB(gr, ast, tg_close_timer_arm);
	struct m0_sm_timer    *timer = &gr->tg_close_timer;
	m0_time_t              deadline = gr->tg_close_deadline;
	int                    rc;

	M0_ENTRY("en=%p gr=%p sm_grp=%p", gr->tg_engine, gr, sm_grp);
	m0_sm_timer_fini(timer);
	m0_sm_timer_init(timer);
	rc = m0_sm_timer_start(timer, sm_grp, &be_engine_group_timer_cb,
			       deadline);
	M0_ASSERT_INFO(rc == 0, "rc = %d", rc);
	M0_LEAVE();
}

static void be_engine_group_timer_disarm(struct m0_sm_group *sm_grp,
                                         struct m0_sm_ast   *ast)
{
	struct m0_be_tx_group *gr    = M0_AMB(gr, ast, tg_close_timer_disarm);
	struct m0_be_engine   *en    = gr->tg_engine;
	struct m0_sm_timer    *timer = &gr->tg_close_timer;

	M0_ENTRY("en=%p gr=%p sm_grp=%p", en, gr, sm_grp);
	if (m0_sm_timer_is_armed(timer))
		m0_sm_timer_cancel(timer);
	m0_sm_ast_cancel(sm_grp, &gr->tg_close_timer_arm);
	M0_LEAVE();
}

static void be_engine_group_freeze(struct m0_be_engine   *en,
                                   struct m0_be_tx_group *gr)
{
	M0_PRE(be_engine_is_locked(en));

	if (gr->tg_state == M0_BGS_OPEN)
		be_engine_tx_group_state_move(en, gr, M0_BGS_FROZEN);
}

static void be_engine_group_tryclose(struct m0_be_engine   *en,
                                     struct m0_be_tx_group *gr)
{
	M0_PRE(be_engine_is_locked(en));

	if (gr->tg_nr_unclosed == 0 && gr->tg_state == M0_BGS_FROZEN) {
		be_engine_tx_group_state_move(en, gr, M0_BGS_CLOSED);
		m0_be_tx_group_close(gr);
		gr->tg_close_timer_disarm.sa_cb = &be_engine_group_timer_disarm;
		m0_sm_ast_post(m0_be_tx_group__sm_group(gr),
			       &gr->tg_close_timer_disarm);
	}
}

static void be_engine_group_timeout_arm(struct m0_be_engine   *en,
                                        struct m0_be_tx_group *gr)
{
	struct m0_sm_group *sm_grp = m0_be_tx_group__sm_group(gr);

	M0_ENTRY("en=%p gr=%p sm_grp=%p", en, gr, sm_grp);
	M0_PRE(be_engine_is_locked(en));

	gr->tg_close_deadline = m0_time_now() +
				en->eng_cfg->bec_group_freeze_timeout;
	gr->tg_close_timer_arm.sa_cb = &be_engine_group_timer_arm;
	m0_sm_ast_post(sm_grp, &gr->tg_close_timer_arm);
	M0_LEAVE();
}

static struct m0_be_tx_group *be_engine_group_find(struct m0_be_engine *en)
{
	M0_PRE(en->eng_cfg->bec_group_nr == 1);

	return m0_tl_find(egr, gr, &en->eng_groups[M0_BGS_OPEN],
	                  !m0_be_tx_group_is_recovering(gr));
}

static int be_engine_tx_trygroup(struct m0_be_engine *en,
				 struct m0_be_tx     *tx)
{
	struct m0_be_tx_group *gr;
	int                    rc = -EBUSY;

	M0_PRE(be_engine_is_locked(en));
	M0_PRE(!m0_be_tx__is_recovering(tx));

	while ((gr = be_engine_group_find(en)) != NULL) {
		if (m0_be_tx__is_exclusive(tx) && m0_be_tx_group_tx_nr(gr) > 0)
			rc = -EBUSY;
		else {
			rc = m0_be_tx_group_tx_add(gr, tx);
			if (rc == 0)
				m0_be_tx__group_assign(tx, gr);
		}
		if (rc != 0 ||
		    m0_be_tx__is_fast(tx) ||
		    m0_be_tx__is_exclusive(tx)) {
			be_engine_group_freeze(en, gr);
		} else if (m0_be_tx_group_tx_nr(gr) == 1) {
			be_engine_group_timeout_arm(en, gr);
		}
		if (rc != 0)
			be_engine_group_tryclose(en, gr);
		if (rc == 0)
			break;
	}
	if (rc == 0)
		be_engine_tx_state_post(en, tx, M0_BTS_ACTIVE);
	return M0_RC(rc);
}

static void be_engine_try_recovery(struct m0_be_engine *en)
{
	struct m0_be_tx_group *gr;
	bool                   group_recovery_started = false;

	M0_ENTRY();

	while (m0_be_recovery_log_record_available(en->eng_recovery)) {
		gr = be_engine_group_find(en);
		if (gr == NULL)
			break;
		m0_be_tx_group_recovery_prepare(gr, en->eng_recovery);
		be_engine_group_freeze(en, gr);
		be_engine_group_tryclose(en, gr);
		group_recovery_started = true;
	}
	/* XXX it will work only with one tx group */
	M0_ASSERT(en->eng_cfg->bec_group_nr == 1);
	if (!group_recovery_started &&
	    !m0_be_recovery_log_record_available(en->eng_recovery) &&
	    !en->eng_recovery_finished) {
		en->eng_recovery_finished = true;
		m0_semaphore_up(&en->eng_recovery_wait_sem);
	}
	M0_LEAVE("group_recovery_started=%d eng_recovery_finished=%d",
		 !!group_recovery_started, !!en->eng_recovery_finished);
}

static struct m0_be_tx *be_engine_recovery_tx_find(struct m0_be_engine *en,
						   enum m0_be_tx_state  state)
{
	return m0_tl_find(etx, tx, &en->eng_txs[state],
			  m0_be_tx__is_recovering(tx));
}

/* XXX RENAME IT */
static void be_engine_got_tx_grouping(struct m0_be_engine *en)
{
	struct m0_be_tx *tx;
	int              rc;

	M0_PRE(be_engine_is_locked(en));

	/* Close recovering transactions */
	while ((tx = be_engine_recovery_tx_find(en, M0_BTS_GROUPING)) != NULL) {
		/*
		 * Group is already closed, we just need to add tx to the group.
		 */
		M0_ASSERT(m0_be_tx_group_is_recovering(tx->t_group));
		M0_ASSERT(tx->t_group->tg_state == M0_BGS_CLOSED);
		rc = m0_be_tx_group_tx_add(tx->t_group, tx);
		M0_ASSERT_INFO(rc == 0, "rc = %d", rc);
		be_engine_tx_state_post(en, tx, M0_BTS_ACTIVE);
	}
	/* Close regular transactions */
	while ((tx = be_engine_tx_peek(en, M0_BTS_GROUPING)) != NULL) {
		rc = be_engine_tx_trygroup(en, tx);
		if (rc != 0)
			break;
	}
}
static void be_engine_got_tx_closed(struct m0_be_engine *en,
				    struct m0_be_tx     *tx)
{
	struct m0_be_tx_group *gr = tx->t_group;

	M0_PRE(be_engine_is_locked(en));

	M0_CNT_DEC(gr->tg_nr_unclosed);
	be_engine_group_tryclose(en, gr);
}

static void be_engine_got_tx_done(struct m0_be_engine *en, struct m0_be_tx *tx)
{
	struct m0_be_tx_group *gr = tx->t_group;

	M0_PRE(be_engine_is_locked(en));

	M0_CNT_DEC(gr->tg_nr_unstable);
	if (gr->tg_nr_unstable == 0)
		m0_be_tx_group_stable(gr);
	tx->t_group = NULL;

	if (m0_be_tx__is_exclusive(tx)) {
		M0_ASSERT(is_engine_at_stopped_or_done(en, false));
		en->eng_exclusive_mode = false;
	}
}

M0_INTERNAL bool m0_be_engine__invariant(struct m0_be_engine *en)
{
	bool rc_bool;

	be_engine_lock(en);
	rc_bool = be_engine_invariant(en);
	be_engine_unlock(en);

	return M0_RC(rc_bool);
}

static void be_engine_gen_idx_min_update(struct m0_be_engine *en)
{
	struct m0_be_tx *tx;
	unsigned long    tx_gen_idx_min;
	unsigned long    en_gen_idx_min;

	M0_PRE(be_engine_is_locked(en));

	tx = efc_tlist_head(&en->eng_tx_first_capture);
	tx_gen_idx_min = tx == NULL ? ULONG_MAX : tx->t_gen_idx_min;
	en_gen_idx_min = en->eng_reg_area_prune_gen_idx_min;
	en->eng_reg_area_prune_gen_idx_min = en_gen_idx_min == ULONG_MAX ?
		tx_gen_idx_min : max_check(en_gen_idx_min, tx_gen_idx_min);
}

static void be_engine_tx_first_capture_add(struct m0_be_engine *en,
					   struct m0_be_tx     *tx)
{
	struct m0_be_tx *tx_before;

	M0_PRE(be_engine_is_locked(en));

	tx_before = efc_tlist_tail(&en->eng_tx_first_capture);
	while (tx_before != NULL &&
	       tx_before->t_gen_idx_min > tx->t_gen_idx_min) {
		tx_before = efc_tlist_prev(&en->eng_tx_first_capture,
					   tx_before);
	}
	if (tx_before == NULL)
		efc_tlist_add(&en->eng_tx_first_capture, tx);
	else
		efc_tlist_add_after(tx_before, tx);

	/*
	 * Check if eng_tx_first_capture is ordered.
	 * This check can be made expensive if it appears in perf top.
	 */
	M0_ASSERT(m0_tl_forall(efc, tx, &en->eng_tx_first_capture,
	       efc_tlist_next(&en->eng_tx_first_capture, tx) == NULL ||
	       tx->t_gen_idx_min <=
	       efc_tlist_next(&en->eng_tx_first_capture, tx)->t_gen_idx_min));

	be_engine_gen_idx_min_update(en);
}

static void be_engine_tx_first_capture_del(struct m0_be_engine *en,
					   struct m0_be_tx     *tx)
{
	M0_PRE(be_engine_is_locked(en));

	if (efc_tlink_is_in(tx))
		efc_tlist_del(tx);
	be_engine_gen_idx_min_update(en);
}

M0_INTERNAL void m0_be_engine__tx_init(struct m0_be_engine *en,
				       struct m0_be_tx     *tx,
				       enum m0_be_tx_state  state)
{
	etx_tlink_init(tx);
	efc_tlink_init(tx);
	m0_be_engine__tx_state_set(en, tx, state);
}

M0_INTERNAL void m0_be_engine__tx_fini(struct m0_be_engine *en,
				       struct m0_be_tx     *tx)
{
	be_engine_lock(en);
	etx_tlink_del_fini(tx);
	be_engine_tx_first_capture_del(en, tx);
	be_engine_unlock(en);

	efc_tlink_fini(tx);
}

M0_INTERNAL void m0_be_engine__tx_state_set(struct m0_be_engine *en,
					    struct m0_be_tx     *tx,
					    enum m0_be_tx_state  state)
{
	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	M0_LOG(M0_DEBUG, "tx %p: => %s", tx, m0_be_tx_state_name(state));

	if (state != M0_BTS_PREPARE)
		etx_tlist_del(tx);
	etx_tlist_add_tail(&en->eng_txs[state], tx);

	switch (state) {
	case M0_BTS_PREPARE:
		/* TODO don't assign id for recovering tx */
		tx->t_id = be_engine_tx_id_allocate(en);
		break;
	case M0_BTS_OPENING:
		be_engine_got_tx_open(en, tx);
		break;
	case M0_BTS_GROUPING:
		be_engine_got_tx_grouping(en);
		break;
	case M0_BTS_CLOSED:
		be_engine_got_tx_closed(en, tx);
		break;
	case M0_BTS_DONE:
		be_engine_got_tx_done(en, tx);
		break;
	case M0_BTS_FAILED:
		if (tx->t_log_reserved)
			m0_be_log_unreserve(&en->eng_log,
					    tx->t_log_reserved_size);
		break;
	default:
		break;
	}

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);
}

M0_INTERNAL void m0_be_engine__tx_force(struct m0_be_engine *en,
					struct m0_be_tx     *tx)
{
	struct m0_be_tx_group *grp;


	/*
	 * Note: as multiple txs may try to move tx group's fom (for example,
	 * a new tx is added to the tx group or multiple txs call
	 * m0_be_tx_force()), we use be engine's lock here
	 */
	be_engine_lock(en);

	grp = tx->t_group;
	if (grp == NULL) {
		be_engine_unlock(en);
		return;
	}

	/*
	 * Is it possible that the tx has been committed to disk while
	 * we were waiting for the lock?
	 */
	/* XXX race here. Let's disable this completely. */
	// if (m0_be_tx_state(tx) < M0_BTS_LOGGED)
	// 	be_engine_group_close(en, grp, true);

	be_engine_unlock(en);
}

M0_INTERNAL void m0_be_engine__tx_group_open(struct m0_be_engine   *en,
					     struct m0_be_tx_group *gr)
{
	M0_ENTRY("en=%p gr=%p", en, gr);
	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	be_engine_tx_group_state_move(en, gr, M0_BGS_OPEN);
	be_engine_try_recovery(en);
	be_engine_got_tx_grouping(en);

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);
	M0_LEAVE();
}

M0_INTERNAL void m0_be_engine__tx_group_discard(struct m0_be_engine   *en,
						struct m0_be_tx_group *gr)
{
	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	m0_be_tx_group_discard(gr);

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);
}

static void be_engine_group_stop_nr(struct m0_be_engine *en, size_t nr)
{
	size_t i;

	M0_PRE(be_engine_is_locked(en));

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

M0_INTERNAL int m0_be_engine_start(struct m0_be_engine *en)
{
	m0_time_t recovery_time = 0;
	int       rc = 0;
	size_t    i;

	M0_ENTRY();
	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	for (i = 0; i < en->eng_group_nr; ++i) {
		rc = m0_be_tx_group_start(&en->eng_group[i]);
		if (rc != 0)
			break;
		egr_tlist_add_tail(&en->eng_groups[M0_BGS_OPEN],
				   &en->eng_group[i]);
		en->eng_group[i].tg_state = M0_BGS_OPEN;
	}
	if (rc == 0) {
		recovery_time = m0_time_now();
		be_engine_try_recovery(en);
	} else {
		be_engine_group_stop_nr(en, i);
	}

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);

	if (rc == 0 && en->eng_cfg->bec_wait_for_recovery) {
		m0_semaphore_down(&en->eng_recovery_wait_sem);
		recovery_time = m0_time_now() - recovery_time;
		M0_LOG(M0_INFO, "BE recovery execution time: %lu",
		       recovery_time);
	}
	return M0_RC(rc);
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

M0_INTERNAL void m0_be_engine_got_log_space_cb(struct m0_be_log *log)
{
	struct m0_be_engine *en =
		container_of(log, struct m0_be_engine, eng_log);

	// be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	be_engine_got_tx_open(en, NULL);

	M0_POST(be_engine_invariant(en));
	// be_engine_unlock(en);
}

M0_INTERNAL struct m0_be_tx *m0_be_engine__tx_find(struct m0_be_engine *en,
						   uint64_t             id)
{
	struct m0_be_tx *tx = NULL;
	size_t		 i;

	be_engine_lock(en);
	M0_PRE(be_engine_invariant(en));

	for (i = 0; i < ARRAY_SIZE(en->eng_txs); ++i) {
		tx = m0_tl_find(etx, tx, &en->eng_txs[i], tx->t_id == id);
		if (tx != NULL) {
			if (M0_IN(m0_be_tx_state(tx),
				  (M0_BTS_FAILED, M0_BTS_DONE))) {
				tx = NULL;
			}
			break;
		}
	}

	M0_POST(be_engine_invariant(en));
	be_engine_unlock(en);

	if (tx != NULL)
		m0_be_tx_get(tx);

	return tx;
}

M0_INTERNAL int
m0_be_engine__exclusive_open_invariant(struct m0_be_engine *en,
				       struct m0_be_tx     *excl)
{
	bool ret;

	be_engine_lock(en);

	ret = m0_forall(state, M0_BTS_DONE, state < M0_BTS_CLOSED ? true :
			etx_tlist_is_empty(&en->eng_txs[state])) &&
		etx_tlist_length(&en->eng_txs[M0_BTS_ACTIVE]) == 1 &&
		etx_tlist_head(&en->eng_txs[M0_BTS_ACTIVE]) == excl;

	be_engine_unlock(en);
	return ret;
}

M0_INTERNAL struct m0_be_tx_credit
m0_be_engine_tx_size_max(struct m0_be_engine *en)
{
	return en->eng_cfg->bec_tx_size_max;
}

M0_INTERNAL void m0_be_engine__tx_first_capture(struct m0_be_engine *en,
						struct m0_be_tx     *tx,
						unsigned long        gen_idx)
{
	tx->t_gen_idx_min = gen_idx;

	be_engine_lock(en);
	be_engine_tx_first_capture_add(en, tx);
	be_engine_unlock(en);
}

M0_INTERNAL void m0_be_engine__reg_area_lock(struct m0_be_engine *en)
{
	m0_mutex_lock(&en->eng_reg_area_lock);
}

M0_INTERNAL void m0_be_engine__reg_area_unlock(struct m0_be_engine *en)
{
	m0_mutex_unlock(&en->eng_reg_area_lock);
}

static bool be_engine_reg_area_is_locked(struct m0_be_engine *en)
{
	return m0_mutex_is_locked(&en->eng_reg_area_lock);
}

M0_INTERNAL void m0_be_engine__reg_area_prune(struct m0_be_engine *en)
{
	M0_PRE(be_engine_reg_area_is_locked(en));

	m0_be_reg_area_prune(&en->eng_reg_area[en->eng_reg_area_index],
	                     en->eng_reg_area_prune_gen_idx_min);
}

M0_INTERNAL void
m0_be_engine__reg_area_rebuild(struct m0_be_engine   *en,
			       struct m0_be_reg_area *group,
			       struct m0_be_reg_area *group_new)
{
	M0_PRE(be_engine_reg_area_is_locked(en));

	m0_be_reg_area_rebuild(&en->eng_reg_area[en->eng_reg_area_index],
			       group,
			       &en->eng_reg_area[1 - en->eng_reg_area_index],
			       group_new);
	m0_be_reg_area_reset(&en->eng_reg_area[en->eng_reg_area_index]);
	/* switch between two global reg_areas */
	en->eng_reg_area_index = 1 - en->eng_reg_area_index;

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
