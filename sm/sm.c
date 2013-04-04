/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 04/01/2010
 */

#include "lib/errno.h"              /* ESRCH */
#include "lib/misc.h"               /* M0_SET0 */
#include "lib/cdefs.h"              /* M0_EXPORTED */
#include "lib/mutex.h"
#include "lib/arith.h"              /* m0_is_po2 */
#include "lib/trace.h"
#include "lib/memory.h"
#include "sm/sm.h"
#include "lib/finject.h"

/**
   @addtogroup sm
   @{
*/

/**
 * An end-of-queue marker.
 *
 * All fork queues end with this pointer. This marker is used instead of NULL to
 * make (ast->sa_next == NULL) equivalent to "the ast is not in a fork queue".
 *
 * Compare with lib/queue.c:EOQ.
 */
static struct m0_sm_ast eoq;

M0_INTERNAL void m0_sm_group_init(struct m0_sm_group *grp)
{
	M0_SET0(grp);
	grp->s_forkq = &eoq;
	m0_mutex_init(&grp->s_lock);
	/* add grp->s_clink to otherwise unused grp->s_chan, because m0_chan
	   code assumes that a clink is always associated with a channel. */
	m0_chan_init(&grp->s_chan, &grp->s_lock);
	m0_clink_init(&grp->s_clink, NULL);
	m0_clink_add_lock(&grp->s_chan, &grp->s_clink);
}

M0_INTERNAL void m0_sm_group_fini(struct m0_sm_group *grp)
{
	if (m0_clink_is_armed(&grp->s_clink))
		m0_clink_del_lock(&grp->s_clink);
	m0_clink_fini(&grp->s_clink);
	m0_chan_fini_lock(&grp->s_chan);
	m0_mutex_fini(&grp->s_lock);
}

M0_INTERNAL void m0_sm_group_lock(struct m0_sm_group *grp)
{
	m0_mutex_lock(&grp->s_lock);
	m0_sm_asts_run(grp);
}

M0_INTERNAL void m0_sm_group_unlock(struct m0_sm_group *grp)
{
	m0_sm_asts_run(grp);
	m0_mutex_unlock(&grp->s_lock);
}

static bool grp_is_locked(const struct m0_sm_group *grp)
{
	return m0_mutex_is_locked(&grp->s_lock);
}

M0_INTERNAL void m0_sm_ast_post(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	M0_PRE(ast->sa_cb != NULL);

	do
		ast->sa_next = grp->s_forkq;
	while (!M0_ATOMIC64_CAS(&grp->s_forkq, ast->sa_next, ast));
	m0_clink_signal(&grp->s_clink);
}

M0_INTERNAL void m0_sm_asts_run(struct m0_sm_group *grp)
{
	struct m0_sm_ast *ast;

	M0_PRE(grp_is_locked(grp));

	while (1) {
		do
			ast = grp->s_forkq;
		while (ast != &eoq &&
		       !M0_ATOMIC64_CAS(&grp->s_forkq, ast, ast->sa_next));

		if (ast == &eoq)
			break;

		ast->sa_next = NULL;
		ast->sa_cb(grp, ast);
	}
}

M0_INTERNAL void m0_sm_ast_cancel(struct m0_sm_group *grp,
				  struct m0_sm_ast *ast)
{
	M0_PRE(grp_is_locked(grp));
	/*
	 * Concurrency: this function runs under the group lock and the only
	 * other possible concurrent fork queue activity is addition of the new
	 * asts at the head of the queue (m0_sm_ast_post()).
	 *
	 * Hence, the queue head is handled specially, with CAS. The rest of the
	 * queue is scanned normally.
	 */
	if (ast->sa_next == NULL)
		; /* not in the queue. */
	else if (grp->s_forkq == ast &&
		 M0_ATOMIC64_CAS(&grp->s_forkq, ast, ast->sa_next))
		; /* deleted the head. */
	else {
		struct m0_sm_ast *prev;
		/*
		 * This loop is safe.
		 *
		 * On the first iteration, grp->s_forkq can be changed
		 * concurrently by m0_sm_ast_post(), but "prev" is still a valid
		 * queue element, because removal from the queue is under the
		 * lock. Newly inserted head elements are not scanned.
		 *
		 * On the iterations after the first, immutable portion of the
		 * queue is scanned.
		 */
		prev = grp->s_forkq;
		while (prev->sa_next != ast)
			prev = prev->sa_next;
		prev->sa_next = ast->sa_next;
	}
	ast->sa_next = NULL;
}

static void sm_lock(struct m0_sm *mach)
{
	m0_sm_group_lock(mach->sm_grp);
}

static void sm_unlock(struct m0_sm *mach)
{
	m0_sm_group_unlock(mach->sm_grp);
}

static bool sm_is_locked(const struct m0_sm *mach)
{
	return grp_is_locked(mach->sm_grp);
}

static bool state_is_valid(const struct m0_sm_conf *conf, uint32_t state)
{
	return
		state < conf->scf_nr_states &&
		conf->scf_state[state].sd_name != NULL;
}

static const struct m0_sm_state_descr *state_get(const struct m0_sm *mach,
						 uint32_t state)
{
	M0_PRE(state_is_valid(mach->sm_conf, state));
	return &mach->sm_conf->scf_state[state];
}

static const struct m0_sm_state_descr *sm_state(const struct m0_sm *mach)
{
	return state_get(mach, mach->sm_state);
}

/**
 * Weaker form of state machine invariant, that doesn't check that the group
 * lock is held. Used in m0_sm_init() and m0_sm_fini().
 */
M0_INTERNAL bool sm_invariant0(const struct m0_sm *mach)
{
	const struct m0_sm_state_descr *sd = sm_state(mach);

	return equi((mach->sm_rc != 0), (sd->sd_flags & M0_SDF_FAILURE)) &&
	       ergo(sd->sd_invariant != NULL, sd->sd_invariant(mach));
}

M0_INTERNAL bool m0_sm_invariant(const struct m0_sm *mach)
{
	return sm_is_locked(mach) && sm_invariant0(mach);
}

static bool conf_invariant(const struct m0_sm_conf *conf)
{
	uint32_t i;
	uint64_t mask;

	if (conf->scf_nr_states >= sizeof(conf->scf_state[0].sd_allowed) * 8)
		return false;

	for (i = 0, mask = 0; i < conf->scf_nr_states; ++i) {
		if (state_is_valid(conf, i))
			mask |= M0_BITS(i);
	}

	for (i = 0; i < conf->scf_nr_states; ++i) {
		if (mask & M0_BITS(i)) {
			const struct m0_sm_state_descr *sd;

			sd = &conf->scf_state[i];
			if (sd->sd_flags & ~(M0_SDF_INITIAL|M0_SDF_FINAL|
					     M0_SDF_FAILURE|M0_SDF_TERMINAL))
				return false;
			if ((sd->sd_flags & M0_SDF_TERMINAL) &&
			    sd->sd_allowed != 0)
				return false;
			if (sd->sd_allowed & ~mask)
				return false;
		}
	}
	return true;
}

M0_INTERNAL void m0_sm_init(struct m0_sm *mach, const struct m0_sm_conf *conf,
			    uint32_t state, struct m0_sm_group *grp)
{
	M0_PRE(conf_invariant(conf));
	M0_PRE(conf->scf_state[state].sd_flags & M0_SDF_INITIAL);

	mach->sm_state = state;
	mach->sm_conf  = conf;
	mach->sm_grp   = grp;
	mach->sm_rc    = 0;
	mach->sm_state_epoch = 0;
	m0_chan_init(&mach->sm_chan, &grp->s_lock);
	M0_POST(sm_invariant0(mach));
}

M0_INTERNAL void m0_sm_fini(struct m0_sm *mach)
{
	M0_ASSERT(sm_invariant0(mach));
	M0_PRE(sm_state(mach)->sd_flags & (M0_SDF_TERMINAL | M0_SDF_FINAL));
	m0_chan_fini(&mach->sm_chan);
}

M0_INTERNAL void m0_sm_conf_init(struct m0_sm_conf *conf)
{
	uint32_t i;
	uint32_t from;
	uint32_t to;

	M0_PRE(!m0_sm_conf_is_initialized(conf));
	M0_PRE(conf->scf_trans_nr > 0);

	M0_ASSERT(conf->scf_nr_states < M0_SM_MAX_STATES);

	for (i = 0; i < conf->scf_nr_states; ++i)
		for (to = 0; to < conf->scf_nr_states; ++to)
			conf->scf_state[i].sd_trans[to] = ~0;

	for (i = 0; i < conf->scf_trans_nr; ++i) {
		from = conf->scf_trans[i].td_src;
		to = conf->scf_trans[i].td_tgt;
		M0_ASSERT(conf->scf_state[from].sd_allowed & M0_BITS(to));
		conf->scf_state[from].sd_trans[to] = i;
	}

	for (i = 0; i < conf->scf_nr_states; ++i)
		for (to = 0; to < conf->scf_nr_states; ++to)
			M0_ASSERT(ergo(conf->scf_state[i].sd_allowed &
			               M0_BITS(to),
			               conf->scf_state[i].sd_trans[to] != ~0));

	conf->scf_magic = M0_SM_CONF_MAGIC;

	M0_POST(m0_sm_conf_is_initialized(conf));
}

M0_INTERNAL bool m0_sm_conf_is_initialized(const struct m0_sm_conf *conf)
{
	return conf->scf_magic == M0_SM_CONF_MAGIC;
}

M0_INTERNAL void m0_sm_stats_enable(struct m0_sm *mach,
				    struct m0_addb_sm_counter *c)
{
	M0_PRE(m0_sm_conf_is_initialized(mach->sm_conf));
	M0_PRE(c->asc_magic == M0_ADDB_CNTR_MAGIC);
	M0_PRE(c->asc_rt->art_sm_conf == mach->sm_conf);

	mach->sm_addb_stats = c;
	mach->sm_state_epoch = m0_time_now();
}

M0_INTERNAL void m0_sm_stats_post(struct m0_sm *mach,
				  struct m0_addb_mc *addb_mc,
				  struct m0_addb_ctx **cv)
{
	if (mach->sm_state_epoch == 0)
		return;

	M0_ASSERT(m0_sm_invariant(mach));

	if (m0_addb_sm_counter_nr(mach->sm_addb_stats) > 0)
		M0_ADDB_POST_SM_CNTR(addb_mc, cv, mach->sm_addb_stats);
}

M0_INTERNAL int m0_sm_timedwait(struct m0_sm *mach, uint64_t states,
				m0_time_t deadline)
{
	struct m0_clink waiter;
	int             result;

	M0_ASSERT(m0_sm_invariant(mach));

	result = 0;
	m0_clink_init(&waiter, NULL);

	m0_clink_add(&mach->sm_chan, &waiter);
	while (result == 0 && (M0_BITS(mach->sm_state) & states) == 0) {
		M0_ASSERT(m0_sm_invariant(mach));
		if (sm_state(mach)->sd_flags & M0_SDF_TERMINAL)
			result = -ESRCH;
		else {
			sm_unlock(mach);
			if (!m0_chan_timedwait(&waiter, deadline))
				result = -ETIMEDOUT;
			sm_lock(mach);
		}
	}
	m0_clink_del(&waiter);
	m0_clink_fini(&waiter);
	M0_ASSERT(m0_sm_invariant(mach));
	return result;
}

static void state_set(struct m0_sm *mach, int state, int32_t rc)
{
	const struct m0_sm_state_descr *sd;

	mach->sm_rc = rc;
	/*
	 * Iterate over a possible chain of state transitions.
	 *
	 * State machine invariant can be temporarily violated because ->sm_rc
	 * is set before ->sm_state is updated and, similarly, ->sd_in() might
	 * set ->sm_rc before the next state is entered. In any case, the
	 * invariant is restored the moment->sm_state is updated and must hold
	 * on the loop termination.
	 */
	do {
		sd = sm_state(mach);
		M0_ASSERT(sd->sd_allowed & M0_BITS(state));
		if (sd->sd_ex != NULL)
			sd->sd_ex(mach);

		/* Update statistics (if enabled) */
		if (mach->sm_state_epoch != 0) {
			m0_time_t now = m0_time_now();
			m0_addb_sm_counter_update(mach->sm_addb_stats,
			    sd->sd_trans[state],
			    m0_time_sub(now, mach->sm_state_epoch) >> 10);
			mach->sm_state_epoch = now;
		}

		mach->sm_state = state;
		M0_ASSERT(m0_sm_invariant(mach));
		sd = sm_state(mach);
		state = sd->sd_in != NULL ? sd->sd_in(mach) : -1;
		m0_chan_broadcast(&mach->sm_chan);
	} while (state >= 0);

	M0_POST(m0_sm_invariant(mach));
}

M0_INTERNAL void m0_sm_fail(struct m0_sm *mach, int fail_state, int32_t rc)
{
	M0_PRE(rc != 0);
	M0_PRE(m0_sm_invariant(mach));
	M0_PRE(mach->sm_rc == 0);
	M0_PRE(state_get(mach, fail_state)->sd_flags & M0_SDF_FAILURE);

	state_set(mach, fail_state, rc);
}

void m0_sm_state_set(struct m0_sm *mach, int state)
{
	M0_PRE(m0_sm_invariant(mach));
	state_set(mach, state, 0);
}
M0_EXPORTED(m0_sm_state_set);

M0_INTERNAL void m0_sm_move(struct m0_sm *mach, int32_t rc, int state)
{
	rc == 0 ? m0_sm_state_set(mach, state) : m0_sm_fail(mach, state, rc);
}

/**
 * m0_sm_timer state machine
 *
 * @verbatim
 *                              INIT
 *                                 |
 *                         +-----+ | m0_sm_timer_start()
 *          sm_timer_top() |     | |
 *                         |     V V
 *                         +----ARMED
 *                               | |
 *          m0_sm_timer_cancel() | | sm_timer_bottom()
 *                               | |
 *                               V V
 *                               DONE
 * @endverbatim
 *
 */
enum timer_state {
	INIT,
	ARMED,
	DONE
};

/**
    Timer call-back for a state machine timer.

    @see m0_sm_timer_start().
*/
static unsigned long sm_timer_top(unsigned long data)
{
	struct m0_sm_timer *timer = (void *)data;

	M0_PRE(M0_IN(timer->tr_state, (ARMED, DONE)));
	/*
	 * no synchronisation or memory barriers are needed here: it's OK to
	 * occasionally post the AST when the timer is already cancelled,
	 * because the ast call-back, synchronised with the cancellation by
	 * the group lock, will sort this out.
	 */
	if (timer->tr_state == ARMED)
		m0_sm_ast_post(timer->tr_grp, &timer->tr_ast);
	return 0;
}

static void timer_done(struct m0_sm_timer *timer)
{
	M0_ASSERT(timer->tr_state == ARMED);

	timer->tr_state = DONE;
	m0_timer_stop(&timer->tr_timer);
}

/**
    AST call-back for a timer.

    @see m0_sm_timer_start().
*/
static void sm_timer_bottom(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_sm_timer *tr = container_of(ast, struct m0_sm_timer, tr_ast);

	M0_PRE(grp_is_locked(tr->tr_grp));
	M0_ASSERT(tr->tr_state == ARMED);

	timer_done(tr);
	tr->tr_cb(tr);
}

M0_INTERNAL void m0_sm_timer_init(struct m0_sm_timer *timer)
{
	M0_SET0(timer);
	timer->tr_state     = INIT;
	timer->tr_ast.sa_cb = sm_timer_bottom;
}

M0_INTERNAL void m0_sm_timer_fini(struct m0_sm_timer *timer)
{
	M0_PRE(M0_IN(timer->tr_state, (INIT, DONE)));
	M0_PRE(timer->tr_ast.sa_next == NULL);

	if (timer->tr_state == DONE) {
		M0_ASSERT(!m0_timer_is_started(&timer->tr_timer));
		m0_timer_fini(&timer->tr_timer);
	}
}

M0_INTERNAL int m0_sm_timer_start(struct m0_sm_timer *timer,
				  struct m0_sm_group *group,
				  void (*cb)(struct m0_sm_timer *),
				  m0_time_t deadline)
{
	int result;

	M0_PRE(grp_is_locked(group));
	M0_PRE(timer->tr_state == INIT);
	M0_PRE(cb != NULL);

	/*
	 * This is how timer is implemented:
	 *
	 *    - a timer is armed (with sm_timer_top() call-back;
	 *
	 *    - when the timer fires off, an AST to the state machine group is
	 *      posted from the timer call-back;
	 *
	 *    - the AST invokes user-supplied call-back.
	 */

	timer->tr_state = ARMED;
	timer->tr_grp   = group;
	timer->tr_cb    = cb;
	m0_timer_init(&timer->tr_timer, M0_TIMER_SOFT, deadline, sm_timer_top,
		      (unsigned long)timer);
	result = m0_timer_start(&timer->tr_timer);
	if (result != 0) {
		timer->tr_state = DONE;
		m0_sm_timer_fini(timer);
	}
	return result;
}

M0_INTERNAL void m0_sm_timer_cancel(struct m0_sm_timer *timer)
{
	M0_PRE(grp_is_locked(timer->tr_grp));
	M0_PRE(M0_IN(timer->tr_state, (ARMED, DONE)));

	if (timer->tr_state == ARMED) {
		timer_done(timer);
		/*
		 * Once timer_done() returned, the timer call-back
		 * (sm_timer_top()) is guaranteed to be never executed, so the
		 * ast won't be posted. Hence, it is safe to remove it, if it
		 * is here.
		 */
		m0_sm_ast_cancel(timer->tr_grp, &timer->tr_ast);
	}
	M0_POST(timer->tr_ast.sa_next == NULL);
}

M0_INTERNAL bool m0_sm_timer_is_armed(const struct m0_sm_timer *timer)
{
	return timer->tr_state == ARMED;
}

/**
    AST call-back for a timeout.

    @see m0_sm_timeout_arm().
*/
static void timeout_ast(struct m0_sm_timer *timer)
{
	struct m0_sm         *mach = timer->tr_ast.sa_mach;
	struct m0_sm_timeout *to   = container_of(timer,
						  struct m0_sm_timeout,
						  st_timer);
	M0_ASSERT(m0_sm_invariant(mach));
	m0_sm_state_set(mach, to->st_state);
}

/**
   Cancels a timeout, if necessary.

   This is called if a state transition happened before the timeout expired.

   @see m0_sm_timeout_arm().
 */
static bool sm_timeout_cancel(struct m0_clink *link)
{
	struct m0_sm_timeout *to   = container_of(link, struct m0_sm_timeout,
						  st_clink);
	struct m0_sm         *mach = to->st_timer.tr_ast.sa_mach;

	M0_ASSERT(m0_sm_invariant(mach));
	if (!(M0_BITS(mach->sm_state) & to->st_bitmask))
		m0_sm_timer_cancel(&to->st_timer);
	return true;
}

M0_INTERNAL void m0_sm_timeout_init(struct m0_sm_timeout *to)
{
	M0_SET0(to);
	m0_sm_timer_init(&to->st_timer);
	m0_clink_init(&to->st_clink, sm_timeout_cancel);
}

M0_INTERNAL int m0_sm_timeout_arm(struct m0_sm *mach, struct m0_sm_timeout *to,
				  m0_time_t timeout, int state,
				  uint64_t bitmask)
{
	int                 result;
	struct m0_sm_timer *tr = &to->st_timer;

	M0_PRE(m0_sm_invariant(mach));
	M0_PRE(tr->tr_state == INIT);
	M0_PRE(!(sm_state(mach)->sd_flags & M0_SDF_TERMINAL));
	M0_PRE(sm_state(mach)->sd_allowed & M0_BITS(state));
	M0_PRE(m0_forall(i, mach->sm_conf->scf_nr_states,
			 ergo(M0_BITS(i) & bitmask,
			      state_get(mach, i)->sd_allowed & M0_BITS(state))));
	if (M0_FI_ENABLED("failed")) return -EINVAL;

	/*
	 * @todo to->st_clink remains registered with mach->sm_chan even after
	 * the timer expires or is cancelled. This does no harm, but should be
	 * fixed when the support for channels with external locks lands.
	 */
	to->st_state       = state;
	to->st_bitmask     = bitmask;
	tr->tr_ast.sa_mach = mach;
	result = m0_sm_timer_start(tr, mach->sm_grp, timeout_ast, timeout);
	if (result == 0)
		m0_clink_add(&mach->sm_chan, &to->st_clink);
	return result;
}

M0_INTERNAL void m0_sm_timeout_fini(struct m0_sm_timeout *to)
{
	m0_sm_timer_fini(&to->st_timer);
	if (m0_clink_is_armed(&to->st_clink))
		m0_clink_del(&to->st_clink);
	m0_clink_fini(&to->st_clink);
}

M0_INTERNAL bool m0_sm_timeout_is_armed(const struct m0_sm_timeout *to)
{
	return m0_sm_timer_is_armed(&to->st_timer);
}

M0_INTERNAL void m0_sm_conf_extend(const struct m0_sm_state_descr *base,
				   struct m0_sm_state_descr *sub, uint32_t nr)
{
	uint32_t i;

	for (i = 0; i < nr; ++i) {
		if (sub[i].sd_name == NULL && base[i].sd_name != NULL)
			sub[i] = base[i];
	}
}

/** @} end of sm group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
