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
#include "lib/mutex.h"
#include "lib/cdefs.h"              /* C2_EXPORTED */
#include "lib/mutex.h"
#include "lib/arith.h"              /* c2_is_po2 */
#include "addb/addb.h"
#include "sm.h"

/**
   @addtogroup sm
   @{
*/

static bool state_is_valid(const struct c2_sm_conf *conf, uint32_t state)
{
	return
		state < conf->scf_nr_states &&
		conf->scf_state[state].sd_name != NULL;
}

static struct c2_sm_state_descr *state_get(const struct c2_sm *mach,
					   uint32_t state)
{
	C2_PRE(state_is_valid(mach->sm_conf, state));
	return &mach->sm_conf->scf_state[state];
}

static struct c2_sm_state_descr *sm_state(const struct c2_sm *mach)
{
	return state_get(mach, mach->sm_state);
}

bool c2_sm_invariant(const struct c2_sm *mach)
{
	struct c2_sm_state_descr *sd = sm_state(mach);

	return
		c2_mutex_is_locked(mach->sm_lock) &&
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
		if (state_is_valid(conf, i)) {
			struct c2_sm_state_descr *sd;

			sd = &conf->scf_state[i];
			if (sd->sd_flags & ~(SDF_FAILURE|SDF_TERMINAL))
				return false;
			if ((sd->sd_flags & SDF_TERMINAL) &&
			    sd->sd_allowed != 0)
				return false;
			if (sd->sd_allowed & ~mask)
				return false;
		}
	}
	return true;
}

void c2_sm_init(struct c2_sm *mach, const struct c2_sm_conf *conf,
		uint32_t state, struct c2_mutex *lock, struct c2_addb_ctx *ctx)
{
	C2_PRE(conf_invariant(conf));

	mach->sm_state = state;
	mach->sm_conf  = conf;
	mach->sm_lock  = lock;
	mach->sm_addb  = ctx;
	mach->sm_rc    = 0;
	c2_chan_init(&mach->sm_chan);
	C2_POST(c2_sm_invariant(mach));
}
C2_EXPORTED(c2_sm_init);

void c2_sm_fini(struct c2_sm *mach)
{
	C2_ASSERT(c2_sm_invariant(mach));
	C2_PRE(sm_state(mach)->sd_flags & SDF_TERMINAL);
	c2_chan_fini(&mach->sm_chan);
}
C2_EXPORTED(c2_sm_fini);

struct wait_state {
	struct c2_clink  ws_waiter;
	struct c2_sm    *ws_mach;
	uint64_t         ws_wake;
};

static bool wait_filter(struct c2_clink *clink)
{
	struct wait_state *ws;

	ws = container_of(clink, struct wait_state, ws_waiter);
	C2_ASSERT(c2_sm_invariant(ws->ws_mach));

	return ((1 << ws->ws_mach->sm_state) & ws->ws_wake) == 0;
}

int c2_sm_timedwait(struct c2_sm *mach, uint64_t states,
		    c2_time_t deadline)
{
	struct c2_clink waiter;

	C2_PRE(c2_mutex_is_locked(mach->sm_lock));

	c2_clink_init(&waiter, wait_filter);
	c2_clink_add(&mach->sm_chan, &waiter);
	while (((1 << mach->sm_state) & states) == 0) {
		C2_ASSERT(c2_sm_invariant(mach));
		if (sm_state(mach)->sd_flags & SDF_TERMINAL)
			return -ESRCH;
		c2_mutex_unlock(mach->sm_lock);
		c2_chan_wait(&waiter);
		c2_mutex_lock(mach->sm_lock);
	}
	C2_ASSERT(c2_sm_invariant(mach));
	return 0;
}
C2_EXPORTED(c2_sm_timedwait);

void c2_sm_fail(struct c2_sm *mach, int fail_state, int32_t rc)
{
	C2_PRE(rc != 0);
	C2_PRE(c2_mutex_is_locked(mach->sm_lock));
	C2_PRE(mach->sm_rc == 0);
	C2_PRE(state_get(mach, fail_state)->sd_flags & SDF_FAILURE);

	c2_sm_state_set(mach, fail_state);
	mach->sm_rc = rc;
}
C2_EXPORTED(c2_sm_fail);

void c2_sm_state_set(struct c2_sm *mach, int state)
{
	struct c2_sm_state_descr *sd;

	C2_PRE(c2_mutex_is_locked(mach->sm_lock));
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
	C2_POST(c2_mutex_is_locked(mach->sm_lock));
}
C2_EXPORTED(c2_sm_state_set);

unsigned long sm_timeout_callback(unsigned long data)
{
}

int c2_sm_timeout(struct c2_sm *mach, struct c2_timer *timer,
		  c2_time_t timeout, int state)
{
	int result;

	result = c2_timer_init(timer, C2_TIMER_SOFT, timeout, 1);
}
C2_EXPORTED(c2_sm_timeout);

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
