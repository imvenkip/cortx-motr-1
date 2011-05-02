/* -*- C -*- */

#ifndef __COLIBRI_SM_H__
#define __COLIBRI_SM_H__

#include "lib/mutex.h"
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

struct c2_sm_conf {
	uint64_t scf_start;
	uint64_t scf_failure;
	uint64_t scf_terminal;
	uint64_t scf_final;
	uint64_t scf_valid;
};

/**
   state machine

   Abstract state machine. Possibly persistent, possibly replicated.
*/
struct c2_sm {
	uint64_t                 sm_state;
	const struct c2_sm_conf *sm_conf;
	struct c2_mutex          sm_lock;
	struct c2_chan           sm_chan;
	int32_t                  sm_rc;
};

void c2_sm_init(struct c2_sm *mach, const struct c2_sm_conf *conf);
void c2_sm_fini(struct c2_sm *mach);

int c2_sm_timedwait(struct c2_sm *mach, uint64_t states,
		    struct c2_time *deadline);

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
