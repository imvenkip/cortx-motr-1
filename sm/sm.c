/* -*- C -*- */

#include "lib/mutex.h"
#include "addb/addb.h"
#include "lib/arith.h"              /* c2_is_po2 */
#include "sm.h"

/**
   @addtogroup sm
   @{
*/

static bool state_is_valid(const struct c2_sm *mach, uint32_t state)
{
	return
		state < mach->sm_conf->scf_nr_states &&
		mach->sm_conf->scf_state[state].sd_name != NULL;
}

static struct c2_sm_state_descr *state_get(const struct c2_sm *mach,
					   uint32_t state)
{
	C2_PRE(state_is_valid(mach, state));
	return &mach->sm_conf->scf_state[state];
}

static struct c2_sm_state_descr *sm_state(const struct c2_sm *mach)
{
	return state_get(mach, mach->sm_state);
}

bool c2_sm_invariant(const struct c2_sm *mach)
{
	struct c2_sm_state_descr *sd = sm_state(sm);

	return
		c2_mutex_is_locked(mach->sm_mutex) &&
		ergo(sd->sd_invariant != NULL, sd->sd_invariant(mach));
}

static bool conf_invariant(const struct c2_sm_conf *conf)
{
	uint32_t i;
	uint64_t mask;

	if (conf->scf_nr_states >= sizeof(conf->scf_state[0].sd_allowed) * 8)
		return false;

	for (i = 0, mask = 0; i < conf->scf_nr_states; ++i) {
		if (state_is_valid(mach, i))
			mask |= (1 << i);
	}

	for (i = 0; i < conf->scf_nr_states; ++i) {
		if (state_is_valid(mach, i)) {
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

	mach0>sm_state = state;
	mach->sm_conf  = conf;
	mach->sm_lock  = lock;
	mach->sm_addb  = ctx;
	mach->sm_rc    = 0;
	c2_chan_init(&mach->sm_chan);
	C2_POST(c2_sm_invariant(mach));
}

void c2_sm_fini(struct c2_sm *mach)
{
	C2_ASSERT(c2_sm_invariant(mach));
	C2_PRE(mach_state(mach)->sd_flags & SDF_TERMINAL);
	c2_chan_fini(&mach->sm_chan);
}

int c2_sm_timedwait(struct c2_sm *mach, uint64_t states,
		    c2_time_t deadline)
{
	struct c2_clink waiter;

	C2_PRE(c2_mutex_is_locked(mach->sm_lock));
	while (((1 << mach->sm_state) & states) == 0) {
		C2_ASSERT(c2_sm_invariant(sm));
		c2_chan_wait();
	}
}

void c2_sm_fail(struct c2_sm *mach, int fail_state, int32_t rc)
{
	C2_PRE(rc != 0);
	C2_PRE(c2_mutex_is_locked(mach->sm_lock));
	C2_PRE(mach->sm_rc == 0);
	C2_PRE(state_get(mach, fail_state)->sd_flags & SDF_FAILURE);

	c2_sm_state_set(mach, fail_state);
	mach->sm_rc = rc;
}

void c2_sm_state_set(struct c2_sm *mach, int state)
{
	struct c2_sm_state_descr *sd;

	C2_PRE(c2_mutex_is_locked(mach->sm_lock));
	C2_PRE(c2_sm_invariant(mach));
	C2_PRE(sd->sd_allowed & (1 << state));

	sd = sm_state(sm);
	if (old->sd_ex != NULL)
		old->sd_ex(mach);
	mach->sd_state = state;
	sd = sm_state(sm);
	if (sd->sd_in != NULL)
		sd->sd_in(mach);
	c2_chan_broadcast(&mach->sm_chan);
	C2_POST(c2_sm_invariant(mach));
	C2_POST(c2_mutex_is_locked(mach->sm_lock));
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
