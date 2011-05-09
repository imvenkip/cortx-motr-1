/* -*- C -*- */

#include "lib/arith.h"              /* c2_is_po2 */
#include "sm.h"

/**
   @addtogroup sm
   @{
*/

static struct c2_sm_state_descr *mach_state(const struct c2_sm *mach)
{
	return &mach->sm_conf->scf_state[mach->sm_state];
}

bool c2_sm_invariant(const struct c2_sm *mach)
{
	const struct c2_sm_state_descr *d;

	d = mach_state(mach);

	return
		mach->sm_state < mach->sm_conf->scf_nr_states &&
		ergo(d->sd_invariant != NULL, d->sd_invariant(mach));
}

void c2_sm_init(struct c2_sm *mach, const struct c2_sm_conf *conf)
{
	C2_PRE(conf->scf_nr_states > 0);

	mach->sm_conf  = conf;
	mach->sm_rc    = 0;
	mach->sm_state = 0;
	c2_mutex_init(&mach->sm_lock);
	c2_chan_init(&mach->sm_chan);

	C2_POST(c2_sm_invariant(mach));
}

void c2_sm_fini(struct c2_sm *mach)
{
	C2_ASSERT(c2_sm_invariant(mach));
	C2_ASSERT(mach_state(mach)->sd_flags & SDF_FINAL);

	c2_chan_fini(&mach->sm_chan);
	c2_mutex_fini(&mach->sm_lock);
}

struct waitinfo {
	struct c2_clink  w_clink;
	struct c2_sm    *w_mach;
	uint64_t         w_states;
};

static bool reached(const struct c2_sm *mach, uint64_t states)
{
	return (1ULL << mach->sm_state) & states;
}

static bool callback(struct c2_clink *clink)
{
	struct waitinfo *wi;

	wi = container_of(clink, struct waitinfo, w_clink);

	C2_ASSERT(c2_mutex_is_locked(&wi->w_mach->sm_lock));
	return !reached(wi->w_mach, wi->w_states);
}

int c2_sm_timedwait(struct c2_sm *mach, uint64_t states,
		    struct c2_time *deadline)
{
	struct waitinfo  wi;
	struct c2_clink *clink;

	clink = &wi.w_clink;
	c2_clink_init(clink, &callback);

	c2_mutex_lock(&mach->sm_lock);
	C2_ASSERT(c2_sm_invariant(mach));

	c2_clink_add(&mach->sm_chan, clink);
	while (!reached(mach, states)) {
		c2_mutex_unlock(&mach->sm_lock);
		c2_chan_timedwait(&wait, deadline);
		c2_mutex_lock(&mach->sm_lock);
		C2_ASSERT(c2_sm_invariant(mach));
	}
	c2_clink_del(clink);
	c2_mutex_unlock(&mach->sm_lock);
	c2_clink_fini(clink);
	return mach->sm_rc;
}

void c2_sm_state_set(struct c2_sm *mach, uint32_t state)
{
	c2_mutex_lock(&mach->sm_lock);
	C2_ASSERT(c2_sm_invariant(mach));
	C2_PRE(state < mach->sm_conf->scf_nr_states);

	C2_ASSERT(c2_bitmap_get(&mach_state(mach)->sd_allowed, state));

	mach->sm_state = state;
	C2_ASSERT(c2_sm_invariant(mach));

	c2_signal_broadcast(&mach->sm_chan);

	c2_mutex_unlock(&mach->sm_lock);
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
