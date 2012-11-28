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
#include "addb/addb.h"
#include "sm/sm.h"
#include "lib/finject.h"

/**
   @addtogroup sm
   @{
*/

M0_INTERNAL void m0_sm_group_init(struct m0_sm_group *grp)
{
	M0_SET0(grp);
	m0_mutex_init(&grp->s_lock);
	/* add grp->s_clink to otherwise unused grp->s_chan, because m0_chan
	   code assumes that a clink is always associated with a channel. */
	m0_chan_init(&grp->s_chan);
	m0_clink_init(&grp->s_clink, NULL);
	m0_clink_add(&grp->s_chan, &grp->s_clink);
}

M0_INTERNAL void m0_sm_group_fini(struct m0_sm_group *grp)
{
	if (m0_clink_is_armed(&grp->s_clink))
		m0_clink_del(&grp->s_clink);
	m0_clink_fini(&grp->s_clink);
	m0_chan_fini(&grp->s_chan);
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
		while (ast != NULL &&
		       !M0_ATOMIC64_CAS(&grp->s_forkq, ast, ast->sa_next));

		if (ast == NULL)
			break;

		ast->sa_next = NULL;
		ast->sa_cb(grp, ast);
	}
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
			mask |= (1 << i);
	}

	for (i = 0; i < conf->scf_nr_states; ++i) {
		if (mask & (1 << i)) {
			const struct m0_sm_state_descr *sd;

			sd = &conf->scf_state[i];
			if (sd->sd_flags & ~(M0_SDF_INITIAL|
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
			    uint32_t state, struct m0_sm_group *grp,
			    struct m0_addb_ctx *ctx)
{
	M0_PRE(conf_invariant(conf));
	M0_PRE(conf->scf_state[state].sd_flags & M0_SDF_INITIAL);

	mach->sm_state = state;
	mach->sm_conf  = conf;
	mach->sm_grp   = grp;
	mach->sm_addb  = ctx;
	mach->sm_rc    = 0;
	m0_chan_init(&mach->sm_chan);
	M0_POST(sm_invariant0(mach));
}

M0_INTERNAL void m0_sm_fini(struct m0_sm *mach)
{
	M0_ASSERT(sm_invariant0(mach));
	M0_PRE(sm_state(mach)->sd_flags & M0_SDF_TERMINAL);
	m0_chan_fini(&mach->sm_chan);
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
	while (result == 0 && ((1 << mach->sm_state) & states) == 0) {
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
		M0_PRE(sd->sd_allowed & (1ULL << state));
		if (sd->sd_ex != NULL)
			sd->sd_ex(mach);
		mach->sm_state = state;
		M0_PRE(m0_sm_invariant(mach));
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
    AST call-back for a timeout.

    @see m0_sm_timeout().
*/
static void sm_timeout_bottom(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_sm_timeout *to   = container_of(ast,
						  struct m0_sm_timeout, st_ast);
	struct m0_sm         *mach = ast->sa_mach;

	M0_ASSERT(m0_sm_invariant(mach));

	if (to->st_active) {
		to->st_active = false;
		m0_sm_state_set(mach, to->st_state);
	}
}

/**
    Timer call-back for a timeout.

    @see m0_sm_timeout().
*/
static unsigned long sm_timeout_top(unsigned long data)
{
	struct m0_sm_timeout *to = (void *)data;

	m0_sm_ast_post(to->st_ast.sa_mach->sm_grp, &to->st_ast);
	return 0;
}

/**
   Cancels a timeout, if necessary.

   This is called if a state transition happened before the timeout expired.

   @see m0_sm_timeout().
 */
static bool sm_timeout_cancel(struct m0_clink *link)
{
	struct m0_sm_timeout *to = container_of(link, struct m0_sm_timeout,
						st_clink);

	M0_ASSERT(m0_sm_invariant(to->st_ast.sa_mach));

	to->st_active = false;
	m0_timer_stop(&to->st_timer);
	return true;
}

M0_INTERNAL int m0_sm_timeout(struct m0_sm *mach, struct m0_sm_timeout *to,
			      m0_time_t timeout, int state)
{
	int              result;
	struct m0_timer *tm = &to->st_timer;

	M0_PRE(m0_sm_invariant(mach));
	M0_PRE(!(sm_state(mach)->sd_flags & M0_SDF_TERMINAL));
	M0_PRE(sm_state(mach)->sd_allowed & (1 << state));
	M0_PRE(!(state_get(mach, state)->sd_flags & M0_SDF_TERMINAL));

	/*
	  This is how timeout is implemented:

	      - a timer is armed (with sm_timeout_top() call-back);

	      - when the timer fires off, an AST (with sm_timeout_bottom()
                call-back) is posted from the timer call-back;

	      - when the AST is executed, it performs the state transition.
	 */

	M0_SET0(to);
	to->st_active      = true;
	to->st_state       = state;
	to->st_ast.sa_cb   = sm_timeout_bottom;
	to->st_ast.sa_mach = mach;
	m0_clink_init(&to->st_clink, sm_timeout_cancel);
	m0_clink_add(&mach->sm_chan, &to->st_clink);
	m0_timer_init(tm, M0_TIMER_SOFT,
		      timeout, sm_timeout_top,
		      (unsigned long)to);
	result = m0_timer_start(tm);
	if (result != 0)
		m0_sm_timeout_fini(to);
	return result;
}

M0_INTERNAL void m0_sm_timeout_fini(struct m0_sm_timeout *to)
{
	M0_PRE(to->st_ast.sa_next == NULL);

	if (m0_timer_is_started(&to->st_timer))
		m0_timer_stop(&to->st_timer);
	m0_timer_fini(&to->st_timer);
	if (m0_clink_is_armed(&to->st_clink))
		m0_clink_del(&to->st_clink);
	m0_clink_fini(&to->st_clink);
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
