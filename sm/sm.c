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
#include "lib/misc.h"               /* C2_SET0 */
#include "lib/cdefs.h"              /* C2_EXPORTED */
#include "lib/mutex.h"
#include "lib/arith.h"              /* c2_is_po2 */
#include "addb/addb.h"
#include "sm/sm.h"

/**
   @addtogroup sm
   @{
*/

void c2_sm_group_init(struct c2_sm_group *grp)
{
	C2_SET0(grp);
	c2_mutex_init(&grp->s_lock);
	c2_chan_init(&grp->s_signal);
}
C2_EXPORTED(c2_sm_group_init);

void c2_sm_group_fini(struct c2_sm_group *grp)
{
	c2_chan_fini(&grp->s_signal);
	c2_mutex_fini(&grp->s_lock);
}
C2_EXPORTED(c2_sm_group_fini);

void c2_sm_group_lock(struct c2_sm_group *grp)
{
	c2_mutex_lock(&grp->s_lock);
	c2_sm_asts_run(grp);
}
C2_EXPORTED(c2_sm_group_lock);

void c2_sm_group_unlock(struct c2_sm_group *grp)
{
	c2_sm_asts_run(grp);
	c2_mutex_unlock(&grp->s_lock);
}
C2_EXPORTED(c2_sm_group_unlock);

static bool grp_is_locked(const struct c2_sm_group *grp)
{
	return c2_mutex_is_locked(&grp->s_lock);
}

void c2_sm_ast_post(struct c2_sm_group *grp, struct c2_sm_ast *ast)
{
	C2_PRE(ast->sa_cb != NULL);

	do
		ast->sa_next = grp->s_forkq;
	while (!C2_ATOMIC64_CAS(&grp->s_forkq, ast->sa_next, ast));
	c2_chan_signal(&grp->s_signal);
}
C2_EXPORTED(c2_sm_ast_post);

void c2_sm_asts_run(struct c2_sm_group *grp)
{
	struct c2_sm_ast *ast;

	C2_PRE(grp_is_locked(grp));

	while (1) {
		do
			ast = grp->s_forkq;
		while (ast != NULL &&
		       !C2_ATOMIC64_CAS(&grp->s_forkq, ast, ast->sa_next));

		if (ast == NULL)
			break;

		ast->sa_cb(grp, ast);
	}
}
C2_EXPORTED(c2_sm_asts_run);

static void sm_lock(struct c2_sm *mach)
{
	c2_sm_group_lock(mach->sm_grp);
}

static void sm_unlock(struct c2_sm *mach)
{
	c2_sm_group_unlock(mach->sm_grp);
}

static bool sm_is_locked(const struct c2_sm *mach)
{
	return grp_is_locked(mach->sm_grp);
}

static bool state_is_valid(const struct c2_sm_conf *conf, uint32_t state)
{
	return
		state < conf->scf_nr_states &&
		conf->scf_state[state].sd_name != NULL;
}

static const struct c2_sm_state_descr *state_get(const struct c2_sm *mach,
					   uint32_t state)
{
	C2_PRE(state_is_valid(mach->sm_conf, state));
	return &mach->sm_conf->scf_state[state];
}

static const struct c2_sm_state_descr *sm_state(const struct c2_sm *mach)
{
	return state_get(mach, mach->sm_state);
}

bool c2_sm_invariant(const struct c2_sm *mach)
{
	const struct c2_sm_state_descr *sd = sm_state(mach);

	return
		sm_is_locked(mach) &&
		ergo(sd->sd_invariant != NULL, sd->sd_invariant(mach));
}

static bool conf_invariant(const struct c2_sm_conf *conf)
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
			const struct c2_sm_state_descr *sd;

			sd = &conf->scf_state[i];
			if (sd->sd_flags & ~(C2_SDF_INITIAL|
					     C2_SDF_FAILURE|C2_SDF_TERMINAL))
				return false;
			if ((sd->sd_flags & C2_SDF_TERMINAL) &&
			    sd->sd_allowed != 0)
				return false;
			if (sd->sd_allowed & ~mask)
				return false;
		}
	}
	return true;
}

void c2_sm_init(struct c2_sm *mach, const struct c2_sm_conf *conf,
		uint32_t state, struct c2_sm_group *grp,
		struct c2_addb_ctx *ctx)
{
	C2_PRE(conf_invariant(conf));
	C2_PRE(conf->scf_state[state].sd_flags & C2_SDF_INITIAL);

	mach->sm_state = state;
	mach->sm_conf  = conf;
	mach->sm_grp   = grp;
	mach->sm_addb  = ctx;
	mach->sm_rc    = 0;
	c2_chan_init(&mach->sm_chan);
	C2_POST(c2_sm_invariant(mach));
}
C2_EXPORTED(c2_sm_init);

void c2_sm_fini(struct c2_sm *mach)
{
	C2_ASSERT(c2_sm_invariant(mach));
	C2_PRE(sm_state(mach)->sd_flags & C2_SDF_TERMINAL);
	c2_chan_fini(&mach->sm_chan);
}
C2_EXPORTED(c2_sm_fini);

int c2_sm_timedwait(struct c2_sm *mach, uint64_t states, c2_time_t deadline)
{
	struct c2_clink waiter;
	struct c2_clink group_waiter;
	int             result;

	C2_ASSERT(c2_sm_invariant(mach));

	result = 0;
	c2_clink_init(&waiter, NULL);
	c2_clink_attach(&group_waiter, &waiter, NULL);

	c2_clink_add(&mach->sm_chan, &waiter);
	c2_clink_add(&mach->sm_grp->s_signal, &group_waiter);
	while (result == 0 && ((1 << mach->sm_state) & states) == 0) {
		C2_ASSERT(c2_sm_invariant(mach));
		if (sm_state(mach)->sd_flags & C2_SDF_TERMINAL)
			result = -ESRCH;
		else {
			sm_unlock(mach);
			if (!c2_chan_timedwait(&waiter, deadline))
				result = -ETIMEDOUT;
			sm_lock(mach);
		}
	}
	c2_clink_del(&group_waiter);
	c2_clink_fini(&group_waiter);
	c2_clink_del(&waiter);
	c2_clink_fini(&waiter);
	C2_ASSERT(c2_sm_invariant(mach));
	return result;
}
C2_EXPORTED(c2_sm_timedwait);

void c2_sm_fail(struct c2_sm *mach, int fail_state, int32_t rc)
{
	C2_PRE(rc != 0);
	C2_PRE(c2_sm_invariant(mach));
	C2_PRE(mach->sm_rc == 0);
	C2_PRE(state_get(mach, fail_state)->sd_flags & C2_SDF_FAILURE);

	c2_sm_state_set(mach, fail_state);
	mach->sm_rc = rc;
}
C2_EXPORTED(c2_sm_fail);

void c2_sm_state_set(struct c2_sm *mach, int state)
{
	const struct c2_sm_state_descr *sd;

	C2_PRE(c2_sm_invariant(mach));

	sd = sm_state(mach);
	C2_PRE(sd->sd_allowed & (1 << state));

	if (sd->sd_ex != NULL)
		sd->sd_ex(mach);
	mach->sm_state = state;
	sd = sm_state(mach);
	if (sd->sd_in != NULL)
		sd->sd_in(mach);
	c2_chan_broadcast(&mach->sm_chan);
	C2_POST(c2_sm_invariant(mach));
}
C2_EXPORTED(c2_sm_state_set);

/**
    AST call-back for a timeout.

    @see c2_sm_timeout().
*/
static void sm_timeout_bottom(struct c2_sm_group *grp, struct c2_sm_ast *ast)
{
	struct c2_sm_timeout *to   = container_of(ast,
						  struct c2_sm_timeout, st_ast);
	struct c2_sm         *mach = ast->sa_mach;

	C2_ASSERT(c2_sm_invariant(mach));

	if (to->st_active) {
		to->st_active = false;
		c2_sm_state_set(mach, to->st_state);
	}
}

/**
    Timer call-back for a timeout.

    @see c2_sm_timeout().
*/
static unsigned long sm_timeout_top(unsigned long data)
{
	struct c2_sm_timeout *to = (void *)data;

	c2_sm_ast_post(to->st_ast.sa_mach->sm_grp, &to->st_ast);
	return 0;
}

/**
   Cancels a timeout, if necessary.

   This is called if a state transition happened before the timeout expired.

   @see c2_sm_timeout().
 */
static bool sm_timeout_cancel(struct c2_clink *link)
{
	struct c2_sm_timeout *to = container_of(link, struct c2_sm_timeout,
						st_clink);

	C2_ASSERT(c2_sm_invariant(to->st_ast.sa_mach));

	to->st_active = false;
	c2_timer_stop(&to->st_timer);
	return true;
}

int c2_sm_timeout(struct c2_sm *mach, struct c2_sm_timeout *to,
		  c2_time_t timeout, int state)
{
	int              result;
	struct c2_timer *tm = &to->st_timer;

	C2_PRE(c2_sm_invariant(mach));
	C2_PRE(!(sm_state(mach)->sd_flags & C2_SDF_TERMINAL));
	C2_PRE(sm_state(mach)->sd_allowed & (1 << state));
	C2_PRE(!(state_get(mach, state)->sd_flags & C2_SDF_TERMINAL));

	/*
	  This is how timeout is implemented:

	      - a timer is armed (with sm_timeout_top() call-back);

	      - when the timer fires off, an AST (with sm_timeout_bottom()
                call-back) is posted from the timer call-back;

	      - when the AST is executed, it performs the state transition.
	 */

	C2_SET0(to);
	to->st_active      = true;
	to->st_state       = state;
	to->st_ast.sa_cb   = sm_timeout_bottom;
	to->st_ast.sa_mach = mach;
	c2_clink_init(&to->st_clink, sm_timeout_cancel);
	c2_clink_add(&mach->sm_chan, &to->st_clink);
	c2_timer_init(tm, C2_TIMER_SOFT,
		      /* XXX kludge: c2_timer_init() takes a _relative_
			 deadline. */
		      c2_time_sub(timeout, c2_time_now()),
		      1, sm_timeout_top,
		      (unsigned long)to);
	result = c2_timer_start(tm);
	if (result != 0)
		c2_sm_timeout_fini(to);
	return result;
}
C2_EXPORTED(c2_sm_timeout);

void c2_sm_timeout_fini(struct c2_sm_timeout *to)
{
	C2_PRE(to->st_ast.sa_next == NULL);

	if (c2_timer_is_started(&to->st_timer))
		c2_timer_stop(&to->st_timer);
	c2_timer_fini(&to->st_timer);
	if (c2_clink_is_armed(&to->st_clink))
		c2_clink_del(&to->st_clink);
	c2_clink_fini(&to->st_clink);
}
C2_EXPORTED(c2_sm_timeout_fini);

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
