/* -*- C -*- */

#ifndef __COLIBRI_SM_SM_H__
#define __COLIBRI_SM_SM_H__

#include "lib/types.h"               /* int32_t, uint64_t */
#include "lib/mutex.h"
#include "lib/chan.h"

/**
   @defgroup sm State machine

   This modules defines interfaces to functionality common to typical
   non-blocking state machines extensively used by Colibri.

   @{
*/

/* export */
struct c2_sm;
struct c2_sm_state_descr;
struct c2_sm_conf;

/* import */
struct c2_addb_ctx;

/**
   state machine.

   Abstract state machine. Possibly persistent, possibly replicated.

   An instance of c2_sm is embedded in a concrete state machine (e.g., a
   per-endpoint rpc layer formation state machine, a resource owner state
   machine (c2_rm_owner), &c.).

   c2_sm stores state machine current state in c2_sm::sm_state. The semantics of
   state are not defined by this module except for classifying states into a few
   broad classes, see c2_sm_state_descr_flags. The only restriction on states is
   that maximal state (as a number) should not be too large, because all states
   are enumerated in a c2_sm::sm_conf::scf_state[] array.

   @invariant c2_sm_invariant() (under mach->sm_lock).
 */
struct c2_sm {
	/**
	   Current state.

	   @invariant mach->sm_state < mach->sm_conf->scf_nr_states
	 */
	uint32_t                 sm_state;
	/**
	   State machine configuration.

	   The configuration enumerates valid state machine states and
	   associates with every state some attributes that are used by c2_sm
	   code to check state transition correctness and to do some generic
	   book-keeping, including addb-based accounting.
	 */
	const struct c2_sm_conf *sm_conf;
	/**
	   Lock under which state transitions happen. This lock is supplied
	   during c2_sm creation so that multiple state machines can share the
	   same lock if necessary.
	 */
	struct c2_mutex         *sm_lock;
	/**
	   addb context in which state machine related events and statistics are
	   reported. This is included by reference for flexibility.
	 */
	struct c2_addb_ctx      *sm_addb;
	/**
	   Channel on which state transitions are announced.
	 */
	struct c2_chan           sm_chan;
	/**
	   State machine "return code". This is set to a non-zero value when
	   state machine transitions to a SDF_FAILURE state.
	 */
	int32_t                  sm_rc;
};

/**
   Configuration describes state machine type.

   c2_sm_conf enumerates possible state machine states.

   @invariant c2_sm_desc_invariant()
 */
struct c2_sm_conf {
	/** Number of states in this state machine. */
	uint32_t                  scf_nr_states;
	/** Array of state descriptions. */
	struct c2_sm_state_descr *scf_state;
};

/**
   Description of some state machine state.
 */
struct c2_sm_state_descr {
	/**
	    Flags, broadly classifying the state, taken from
	    c2_sm_state_descr_flags.
	 */
	uint32_t    sd_flags;
	/**
	    Human readable state name for debugging. This field is NULL for
	    "invalid" states, which state machine may never enter.
	 */
	const char *sd_name;
	/**
	   This function (if non-NULL) is called by c2_sm_state_set() when the
	   state is entered.
	 */
	void      (*sd_in)(struct c2_sm *mach);
	/**
	   This function (if non-NULL) is called by c2_sm_state_set() when the
	   state is left.
	 */
	void      (*sd_ex)(struct c2_sm *mach);
	/**
	   Invariant that must hold while in this state. Specifically, this
	   invariant is checked under the state machine lock once transition to
	   this state completed, checked under the same lock just before a
	   transition out of the state is about to happen and is checked (under
	   the lock) whenever a c2_sm call finds the target state machine in
	   this state.

	   If this field is NULL, no invariant checks are done.
	 */
	bool      (*sd_invariant)(const struct c2_sm *mach);
	/**
	   A bitmap of states to which transitions from this state are allowed.

	   @note this limits the number of states to 64, which should be more
	   than enough. Should a need in extra complicated machines arise, this
	   can be replaced with c2_bitmap, as the expense of making static
	   c2_sm_state_descr more complicated.
	 */
	uint64_t    sd_allowed;
};

/**
   Flags for state classification, used in c2_sm_state_descr::sd_flags.
 */
enum c2_sm_state_descr_flags {
	/**
	   A state marked with this flag is a failure state. c2_sm::sm_rc is set
	   to a non-zero value on entering this state.

	   @see c2_sm_fail()
	 */
	SDF_FAILURE  = 1 << 0,
	/**
	   A state marked with this flag is a terminal state. No transitions out
	   of this state are allowed (checked by c2_sm_conf_invariant()) and an
	   attempt to wait for a state transition, while the state machine is in
	   a terminal state, immediately returns ESRCH.
	 */
	SDF_TERMINAL = 1 << 1,
	SDF_FINAL    = 1 << 2
};

void c2_sm_init(struct c2_sm *mach, const struct c2_sm_conf *conf,
		struct c2_mutex *lock, struct c2_addb_ctx *ctx);
void c2_sm_fini(struct c2_sm *mach);

int c2_sm_timedwait(struct c2_sm *mach, uint64_t states,
		    c2_time_t deadline);

/**
   Move a state machine into fail_state state atomically with setting rc code.

   @pre rc != 0
   @pre c2_mutex_is_locked(mach->sm_lock)
   @pre mach->sm_rc == 0
   @pre mach->sm_conf->scf_state[fail_state].sd_flags & SDF_FAILURE
   @post mach->sm_rc == rc
   @post mach->sm_state == fail_state
   @post c2_mutex_is_locked(mach->sm_lock)
 */
void c2_sm_fail(struct c2_sm *mach, int fail_state, int32_t rc);

/**
   Transit a state machine into the indicated state.

   @pre c2_mutex_is_locked(mach->sm_lock)
   @post mach->sm_state == state
   @post c2_mutex_is_locked(mach->sm_lock)
 */
void c2_sm_state_set(struct c2_sm *mach, int state);

/** @} end of sm group */

/* __COLIBRI_SM_SM_H__ */
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
