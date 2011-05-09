/* -*- C -*- */

#ifndef __COLIBRI_SM_H__
#define __COLIBRI_SM_H__

#include "lib/time.h"
#include "lib/mutex.h"
#include "lib/bitmap.h"
#include "lib/chan.h"

/**
   @defgroup sm State machine
   @{
*/

/* import */
struct c2_time;


enum c2_sm_res {
	SR_DONE,
	SR_AGAIN,
	SR_WAIT
};

struct c2_sm;
struct c2_sm_state_descr;
struct c2_sm_conf;

/**
   state machine

   Abstract state machine. Possibly persistent, possibly replicated.
*/
struct c2_sm {
	uint32_t                 sm_state;
	const struct c2_sm_conf *sm_conf;
	struct c2_mutex          sm_lock;
	struct c2_chan           sm_chan;
	int32_t                  sm_rc;
};

struct c2_sm_conf {
	uint32_t            scf_nr_states;
	struct c2_sm_state *scf_state;
};

struct c2_sm_state_descr {
	uint32_t         sd_flags;
	const char      *sd_name;
	bool           (*sd_invariant)(const struct c2_sm *mach);
	struct c2_bitmap sd_allowed;
};

enum c2_sm_state_descr_flags {
	SDF_FAILURE  = 1 << 0,
	SDF_TERMINAL = 1 << 1,
	SDF_FINAL    = 1 << 2
};

void c2_sm_init(struct c2_sm *mach, const struct c2_sm_conf *conf);
void c2_sm_fini(struct c2_sm *mach);

int c2_sm_timedwait(struct c2_sm *mach, uint64_t states,
		    c2_time_t deadline);

/** @} end of sm group */

/* __COLIBRI_SM_H__ */
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
