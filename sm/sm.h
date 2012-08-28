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

#pragma once

#ifndef __COLIBRI_SM_SM_H__
#define __COLIBRI_SM_SM_H__

#include "lib/types.h"               /* int32_t, uint64_t */
#include "lib/atomic.h"
#include "lib/time.h"                /* c2_time_t */
#include "lib/timer.h"
#include "lib/semaphore.h"
#include "lib/chan.h"
#include "lib/mutex.h"
#include "lib/tlist.h"

/**
   @defgroup sm State machine

   This modules defines interfaces to functionality common to typical
   non-blocking state machines extensively used by Colibri.

   The main difference between "state machine" (non-blocking) code and
   "threaded" (blocking) code is that the latter blocks waiting for some events
   while having some computational state stored in the "native" C language stack
   (in the form of automatic variables allocated across the call-chain). Because
   of this the thread must remain dedicated to the same threaded activity not
   only during actual "computation", when processor is actively used, but also
   for the duration of wait. In many circumstances this is too expensive,
   because threads are heavy objects.

   Non-blocking code, on the other hand, packs all its state into a special
   data-structures before some potential blocking points and unpacks it after
   the event of interest occurs. This allows the same thread to be re-used for
   multiple non-blocking computations.

   Which blocking points deserve packing-unpacking depends on
   circumstances. Long-term waits for network or storage communication are prime
   candidates for non-blocking handling. Memory accesses, which can incur
   blocking page faults in a user space process are probably too ubiquitous for
   this. Memory allocations and data-structure locks fall into an intermediate
   group.

   This module defines data-structures and interfaces to handle common
   non-blocking state-machine functionality:

       - state tracking and state transitions;

       - concurrency;

       - interaction between a state machine and external world (both
         non-blocking and threaded);

       - accounting and statistics collection.

   <b>State and transitions.</b>

   State machine state is recorded in c2_sm::sm_state. This is supposed to be a
   relatively coarse-grained high level state, broadly determining state machine
   behaviour. An instance of c2_sm will typically be embedded into a larger
   structure containing fields fully determining state machine behaviour. Each
   state comes with a description. All descriptions for a particular state
   machine are packed into a c2_sm_conf::scf_state[] array.

   State machine is transferred from one state to another by a call to
   c2_sm_state_set() (or its variant c2_sm_fail()) or via "chained" transitions,
   see below.

   <b>Concurrency.</b>

   State machine is a part of a state machine group (c2_sm_group). All state
   machines in a group use group's mutex to serialise their state
   transitions. One possible scenario is to have a group for all state machines
   associated with a given locality (c2_fom_locality). Alternatively a
   group-per-machine can be used.

   <b>Interaction.</b>

   The only "output" event that a state machine communicates to the external
   world is (from this module's point of view) its state transition. State
   transitions are announces on a per-machine channel (c2_sm::sm_chan). This
   mechanism works both for threaded and non-blocking event consumers. The
   formers use c2_sm_timedwait() to wait until the state machine reaches
   desirable state, the latter register a clink with c2_sm::sm_chan.

   "Input" events cause state transitions. Typical examples of such events are:
   completion of a network of storage communication, timeout or a state
   transition in a different state machine. Such events often happen in
   "awkward" context: signal and interrupt handlers, timer call-backs and
   similar. Acquiring the group's mutex, necessary for state transition in such
   places is undesirable for multiple reasons:

       - to avoid self-deadlock in a case where an interrupt or signal is
         serviced by a thread that already holds the mutex, the latter must be
         made "async-safe", which is quite expensive;

       - implementation of a module that provides a call-back must take into
         account the possibility of the call-back blocking waiting for a
         mutex. This is also quite expensive;

       - locking order dependencies arise between otherwise unrelated
         components;

       - all these issues are exasperated in a situation where state transition
         must take additional locks, which it often does.

   The solution to these problems comes from operating system kernels design,
   see the AST section below.

   There are 2 ways to implement state machine input event processing:

       - "external" state transition, where input event processing is done
         outside of state machine, and c2_sm_state_set() is called to record
         state change:

@code
struct foo {
	struct c2_sm f_sm;
	...
};

void event_X(struct foo *f)
{
	c2_sm_group_lock(f->f_sm.sm_grp);
	process_X(f);
	c2_sm_state_set(&f->f_sm, NEXT_STATE);
	c2_sm_group_unlock(f->f_sm.sm_grp);
}
@endcode

      - "chained" state transition, where event processing logic is encoded in
      c2_sm_state_descr::sd_in() methods and a call to c2_sm_state_set() causes
      actual event processing to happen:

@code
int X_in(struct c2_sm *mach)
{
        struct foo *f = container_of(mach, struct foo, f_sm);
	// group lock is held.
	process_X(f);
	return NEXT_STATE;
}

const struct c2_sm_conf foo_sm_conf = {
	...
	.scf_state = foo_sm_states
};

const struct c2_sm_state_descr foo_sm_states[] = {
	...
	[STATE_X] = {
		...
		.sd_in = X_in
	},
	...
};

void event_X(struct foo *f)
{
	c2_sm_group_lock(f->f_sm.sm_grp);
	// this calls X_in() and goes through the chain of state transitions.
	c2_sm_state_set(&f->f_sm, STATE_X);
	c2_sm_group_unlock(f->f_sm.sm_grp);
}

@endcode

   <b>Accounting and statistics.</b>

   This module accumulates statistics about state transitions and time spent in
   particular states. For additional flexibility these data are reported through
   an addb context supplied during state machine initialisation so that the same
   context can be used for other purposes too.

   @todo accounting and statistics is not currently implemented.

   <b>AST.</b>

   Asynchronous System Trap (AST) is a mechanism that allows a code running in
   an "awkward context" (see above) to post a call-back to be executed at the
   "base level" under a group mutex. UNIX kernels traditionally used a similar
   mechanism, where an interrupt handler does little more than setting a flag
   and returning. This flag is checked when the kernel is just about to return
   to the user space. If the flag is set, the rest of interrupt processing
   happens. In Linux a similar mechanism is called a "top-half" and
   "bottom-half" of interrupt processing. In Windows it is a DPC
   (http://en.wikipedia.org/wiki/Deferred_Procedure_Call) mechanism, in older
   DEC kernels it was called a "fork queue".

   c2_sm_ast structure represents a call-back to be invoked under group
   mutex. An ast is "posted" to a state machine group by a call to
   c2_sm_ast_post(), which can be done in any context, in the sense that it
   doesn't take any locks. Posted asts are executed

       - just after group mutex is taken;

       - just before group mutex is released;

       - whenever c2_sm_asts_run() is called.

   Ast mechanism solves the problems with input events mentioned above at the
   expense of

       - an increased latency: the call-back is not executed immediately;

       - an additional burden of ast-related book-keeping: it is up to the ast
         user to free ast structure when it is safe to do so (i.e., after the
         ast completed execution).

   To deal with the latency problem, a user must arrange c2_sm_asts_run() to be
   called during long state transitions (typically within loops).

   There are a few ways to deal with the ast book-keeping problem:

       - majority of asts will be embedded in some longer living data-structures
         like foms and won't need separate allocation of freeing;

       - some ast users might allocate asts dynamically;

       - the users which have neither a long-living data-structure to embed ast
         in nor can call dynamic allocator, have to pre-allocate a pool of asts
         and to guarantee somehow that it is never exhausted.

   If an ast is posted a c2_sm_group::s_clink clink is signalled. A user
   managing a state machine group might arrange a special "ast" thread (or a
   group of threads) to wait on this channel and to call c2_sm_asts_run() when
   the channel is signalled:

   @code
   while (1) {
           c2_chan_wait(&G->s_clink);
           c2_sm_group_lock(G);
	   c2_sm_asts_run(G);
           c2_sm_group_unlock(G);
   }
   @endcode

   A special "ast" thread is not needed if there is an always running "worker"
   thread or pool of threads associated with the state machine group. In the
   latter case, the worker thread can wait on c2_sm_group::s_clink in addition
   to other channels it waits on (see c2_clink_attach()).

   c2_sm_group_init() initialises c2_sm_group::s_clink with a NULL call-back. If
   a user wants to re-initialise it with a different call-back or to attach it
   to a clink group, it should call c2_clink_fini() followed by c2_clink_init()
   or c2_link_attach() before any state machine is created in the group.

   @{
*/

/* export */
struct c2_sm;
struct c2_sm_state_descr;
struct c2_sm_conf;
struct c2_sm_group;
struct c2_sm_ast;

/* import */
struct c2_addb_ctx;
struct c2_timer;
struct c2_mutex;

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

   @invariant c2_sm_invariant() (under mach->sm_grp->s_lock).
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
	struct c2_sm_group      *sm_grp;
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
	   state machine transitions to an C2_SDF_FAILURE state.
	 */
	int32_t                  sm_rc;
};

/**
   Configuration describes state machine type.

   c2_sm_conf enumerates possible state machine states.

   @invariant c2_sm_desc_invariant()
 */
struct c2_sm_conf {
	const char                     *scf_name;
	/** Number of states in this state machine. */
	uint32_t                        scf_nr_states;
	/** Array of state descriptions. */
	const struct c2_sm_state_descr *scf_state;
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

	   If this function returns a non-negative number, the state machine
	   immediately transits to the returned state (this transition includes
	   all usual side-effects, like machine channel broadcast and invocation
	   of the target state ->sd_in() method). This process repeats until
	   ->sd_in() returns negative number. Such state transitions are called
	   "chained", see "chain" UT for examples. To fail the state machine,
	   set c2_sm::sm_rc manually and return one of C2_SDF_FAILURE states,
	   see C_OVER -> C_LOSE transition in the "chain" UT.
	 */
	int       (*sd_in)(struct c2_sm *mach);
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
	    An initial state.

	    State machine, must start execution in a state marked with this
	    flag. Multiple states can be marked with this flag, for example, to
	    share a code between similar state machines, that only differ in
	    initial conditions.

	    @see c2_sm_init()
	 */
	C2_SDF_INITIAL  = 1 << 0,
	/**
	   A state marked with this flag is a failure state. c2_sm::sm_rc is set
	   to a non-zero value on entering this state.

	   In a such state, state machine is supposed to handle or report the
	   error indicated by c2_sm::sm_rc. Typically (but not necessary), the
	   state machine will transit into am C2_SDF_TERMINAL state immediately
	   after a failure state.

	   @see c2_sm_fail()
	 */
	C2_SDF_FAILURE  = 1 << 1,
	/**
	   A state marked with this flag is a terminal state. No transitions out
	   of this state are allowed (checked by c2_sm_conf_invariant()) and an
	   attempt to wait for a state transition, while the state machine is in
	   a terminal state, immediately returns -ESRCH.

	   @see c2_sm_timedwait()
	 */
	C2_SDF_TERMINAL = 1 << 2
};

/**
   Asynchronous system trap.

   A request to execute a call-back under group mutex. An ast can be posted by a
   call to c2_sm_ast_post() in any context.

   It will be executed later, see AST section of the comment at the top of this
   file.

   Only c2_sm_ast::sa_cb and c2_sm_ast::sa_datum fields are public. The rest of
   this structure is for internal use by sm code.
 */
struct c2_sm_ast {
	/** Call-back to be executed. */
	void              (*sa_cb)(struct c2_sm_group *grp, struct c2_sm_ast *);
	/** This field is reserved for the user and not used by the sm code. */
	void               *sa_datum;
	struct c2_sm_ast   *sa_next;
	struct c2_sm       *sa_mach;
};

struct c2_sm_group {
	struct c2_mutex   s_lock;
	struct c2_clink   s_clink;
	struct c2_sm_ast *s_forkq;
	struct c2_chan    s_chan;
};

/**
   Initialises a state machine.

   @pre conf->scf_state[state].sd_flags & C2_SDF_INITIAL
 */
void c2_sm_init(struct c2_sm *mach, const struct c2_sm_conf *conf,
		uint32_t state, struct c2_sm_group *grp,
		struct c2_addb_ctx *ctx);
/**
   Finalises a state machine.

   @pre conf->scf_state[state].sd_flags & C2_SDF_TERMINAL
 */
void c2_sm_fini(struct c2_sm *mach);

void c2_sm_group_init(struct c2_sm_group *grp);
void c2_sm_group_fini(struct c2_sm_group *grp);

void c2_sm_group_lock(struct c2_sm_group *grp);
void c2_sm_group_unlock(struct c2_sm_group *grp);

/**
   Waits until a given state machine enters any of states enumerated by a given
   bit-mask.

   @retval 0          - one of the states reached

   @retval -ESRCH     - terminal state reached,
                        see c2_sm_state_descr_flags::C2_SDF_TERMINAL

   @retval -ETIMEDOUT - deadline passed

   In case where multiple wait termination conditions hold simultaneously (e.g.,
   @states includes a terminal state), the result is implementation dependent.

   @note this interface assumes that states are numbered by numbers less than
   64.
 */
int c2_sm_timedwait(struct c2_sm *mach, uint64_t states, c2_time_t deadline);

/**
   Moves a state machine into fail_state state atomically with setting rc code.

   @pre rc != 0
   @pre c2_mutex_is_locked(&mach->sm_grp->s_lock)
   @pre mach->sm_rc == 0
   @pre mach->sm_conf->scf_state[fail_state].sd_flags & C2_SDF_FAILURE
   @post mach->sm_rc == rc
   @post mach->sm_state == fail_state
   @post c2_mutex_is_locked(&mach->sm_grp->s_lock)
 */
void c2_sm_fail(struct c2_sm *mach, int fail_state, int32_t rc);

/**
   Transits a state machine into the indicated state.

   Calls ex- and in- methods of the corresponding states (even if the state
   doesn't change after all).

   The (mach->sm_state == state) post-condition cannot be asserted, because of
   chained state transitions.

   @pre c2_mutex_is_locked(&mach->sm_grp->s_lock)
   @post c2_mutex_is_locked(&mach->sm_grp->s_lock)
 */
void c2_sm_state_set(struct c2_sm *mach, int state);

/**
   Structure used by c2_sm_timeout() to record timeout state.

   This structure is owned by the sm code, user should not access it. The user
   provides uninitialised instance c2_sm_timeout to c2_sm_timeout(). The
   instance can be freed after the next state transition for the state machine
   completes, see c2_sm_timeout() for details.
 */
struct c2_sm_timeout {
	/** Timer used to implement delayed state transition. */
	struct c2_timer  st_timer;
	/** Clink to watch for state transitions that might cancel the
	    timeout. */
	struct c2_clink  st_clink;
	/** AST invoked when timer fires off. */
	struct c2_sm_ast st_ast;
	/** Target state. */
	int              st_state;
	/** True if this timeout neither expired nor cancelled. */
	bool             st_active;
};

/**
   Arms a timer to move a machine into a given state after a given timeout.

   If a state transition happens before the timeout expires, the timeout is
   cancelled.

   It is possible to arms multiple timeouts against the same state machine. The
   first one to expire will cancel the rest.

   The c2_sm_timeout instance, supplied to this call can be freed after timeout
   expires or is cancelled.

   @param timeout absolute time at which the state transition will take place
   @param state the state to which the state machine will transition after the
   timeout.

   @pre c2_mutex_is_locked(&mach->sm_grp->s_lock)
   @pre state transition from current state to the target state is allowed.
   @post c2_mutex_is_locked(&mach->sm_grp->s_lock)
 */
int c2_sm_timeout(struct c2_sm *mach, struct c2_sm_timeout *to,
		  c2_time_t timeout, int state);
/**
   Finaliser that must be called before @to can be freed.
 */
void c2_sm_timeout_fini(struct c2_sm_timeout *to);

/**
   Posts an AST to a group.
 */
void c2_sm_ast_post(struct c2_sm_group *grp, struct c2_sm_ast *ast);

/**
   Runs posted, but not yet executed ASTs.

   @pre c2_mutex_is_locked(&grp->s_lock)
   @post c2_mutex_is_locked(&grp->s_lock)
 */
void c2_sm_asts_run(struct c2_sm_group *grp);

enum c2_sm_return {
	/**
	 * Negative mumbers are used to return from state function without
	 * transitioning to next state.
	 */
	C2_SM_BREAK = -1,
};

/**
 * "Extends" base state descriptions with the given sub descriptions.
 *
 * Updates sub in place to become a merged state machine descriptions array that
 * uses base state descriptors, unless overridden by sub.
 */
void c2_sm_conf_extend(const struct c2_sm_state_descr *base,
		       struct c2_sm_state_descr *sub, uint32_t nr);

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
