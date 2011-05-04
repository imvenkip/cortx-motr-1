/* -*- C -*- */

#include "lib/arith.h"              /* c2_is_po2 */
#include "sm.h"

/**
   @addtogroup sm
   @{
*/

static bool is_subset(uint64_t a, uint64_t b)
{
	return (a & b) == a;
}

static bool belongs(uint64_t a, uint64_t b)
{
	C2_PRE(c2_is_po2(a));
	return a & b;
}

static bool c2_sm_invariant(const struct c2_sm *mach)
{
	const struct c2_sm_conf *c;

	c = mach->sm_conf;
	return is_subset(c->scf_start | c->scf_failure | c->scf_terminal |
			 c->scf_final, c->scf_valid) &&
		c2_is_po2(mach->sm_state) &&
		belongs(mach->sm_state, c->scf_valid);
}

void c2_sm_init(struct c2_sm *mach, const struct c2_sm_conf *conf)
{
	mach->sm_state = conf->scf_start;
	mach->sm_conf  = conf;
	mach->sm_rc    = 0;
	c2_mutex_init(&mach->sm_lock);
	c2_chan_init(&mach->sm_chan);
	C2_POST(c2_sm_invariant(mach));
}

void c2_sm_fini(struct c2_sm *mach)
{
	C2_ASSERT(c2_sm_invariant(mach));
	C2_ASSERT(is_subset(mach->sm_state, mach->sm_conf->scf_final));
	c2_chan_fini(&mach->sm_chan);
	c2_mutex_fini(&mach->sm_lock);
}

int c2_sm_timedwait(struct c2_sm *mach, uint64_t states,
		    struct c2_time *deadline)
{
	struct c2_clink wait;

	c2_clink_init(&wait, NULL);
	c2_clink_add(&mach->sm_chan, &wait);

	c2_mutex_lock(&mach->sm_lock);
	C2_ASSERT(c2_sm_invariant(mach));
	while (!belongs(mach->sm_state,
			states | mach->sm_conf->scf_terminal)) {
		c2_mutex_unlock(&mach->sm_lock);
		c2_chan_timedwait(&wait, deadline);
		c2_mutex_lock(&mach->sm_lock);
		C2_ASSERT(c2_sm_invariant(mach));
	}
	c2_mutex_unlock(&mach->sm_lock);
	return mach->sm_rc;
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
