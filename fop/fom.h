/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>,
 *		    Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/19/2010
 */

#pragma once

#ifndef __COLIBRI_FOP_FOM_H__
#define __COLIBRI_FOP_FOM_H__

/**
 * @defgroup fom Fop state Machine
 *
 * <b>Fop state machine (fom)</b>
 *
 * A fom is a non-blocking state machine. Almost all server-side Colibri
 * activities are implemented as foms. Specifically, all file system operation
 * requests, received from clients are executed as foms (hence the name).
 *
 * Fom execution is controlled by request handler (reqh) which can be thought of
 * as a specialised scheduler. Request handler is similar to a typical OS kernel
 * scheduler: it maintains lists of ready (runnable) and waiting foms. Colibri
 * request handler tries to optimise utilisation of processor caches. To this
 * end, it is partitioned into a set of "localities", typically, one locality
 * for a processor core. Each locality has its own ready and waiting lists and
 * each fom is assigned to a locality.
 *
 * A fom is not associated with any particular thread: each state transition is
 * executed in the context of a certain handler thread, but the next state
 * transition can be executed by a different thread. Usually, all these threads
 * run in the same locality (on the same core), but a fom can be migrated
 * between localities for load-balancing purposes (@todo load balancing is not
 * implemented at the moment).
 *
 * The aim of interfaces defined below is to simplify construction of a
 * non-blocking file server (see HLD referenced below for a more detailed
 * exposition).
 *
 * <b>Fom phase and state</b>
 *
 * Fom operates by moving from "phase" to "phase". Current phase is recorded in
 * c2_fom::fo_phase. Generic code in fom.c pays no attention to this field,
 * except for special C2_FOM_PHASE_INIT and C2_FOM_PHASE_FINI values used to
 * control fom life-time. ->fo_phase value is interpreted by fom-type-specific
 * code. core/reqh/ defines some "standard phases", that a typical fom related
 * to file operation processing passes through.
 *
 * Each phase transition should be non-blocking. When a fom cannot move to the
 * next phase immediately, it waits for an event that would make non-blocking
 * phase transition possible.
 *
 * Internally, request handler maintains, in addition to phase, a
 * c2_fom::fo_state field, recording fom state, which can be RUNNING
 * (C2_FOS_RUNNING), READY (C2_FOS_READY) and WAITING (C2_FOS_WAITING). A fom is
 * in RUNNING state, when its phase transition is currently being executed. A
 * fom is in READY state, when its phase transition can be executed immediately
 * and a fom is in WAITING state, when no phase transition can be executed
 * immediately.
 *
 * Request handler, according to some policy, selects a fom in READY state,
 * moves it to RUNNING state and calls its c2_fom_ops::fo_tick() function to
 * execute phase transition. This function has 2 possible return values:
 *
 *     - C2_FSO_AGAIN: more phase transitions are possible immediately. When
 *       this value is returned, request handler returns the fom back to the
 *       READY state and guarantees that c2_fom_ops::fo_tick() will be called
 *       eventually as determined by policy. The reason to return C2_FSO_AGAIN
 *       instead of immediately executing the next phase transition right within
 *       ->fo_tick() is to get request handler a better chance to optimise
 *       performance globally, by selecting the "best" READY fom;
 *
 *     - C2_FSO_WAIT: no phase transitions are possible at the moment. As a
 *       special case, if fom->fo_phase == C2_FOM_PHASE_FINI, request handler
 *       destroys the fom, by calling its c2_fom_ops::fo_fini()
 *       method. Otherwise, the fom is placed in WAITING state.
 *
 * A fom moves WAITING to READY state by the following means:
 *
 *     - before returning C2_FSO_WAIT, its ->fo_tick() function can arrange a
 *       wakeup, by calling c2_fom_wait_on(fom, chan, cb). When chan is
 *       signalled, the fom is moved to READY state. More generally,
 *       c2_fom_callback_arm(fom, chan, cb) call arranges for an arbitrary
 *       call-back to be called when the chan is signalled. The call-back can
 *       wake up the fom by calling c2_fom_ready();
 *
 *     - a WAITING fom can be woken up by calling c2_fom_wakeup() function that
 *       moves it in READY state.
 *
 * These two methods should not be mixed: internally they use the same
 * data-structure c2_fom::fo_cb.fc_ast.
 *
 * Typical ->fo_tick() function looks like
 *
 * @code
 * static int foo_tick(struct c2_fom *fom)
 * {
 *         struct foo_fom *obj = container_of(fom, struct foo_fom, ff_base);
 *
 *         if (fom->fo_phase < C2_FOPH_NR)
 *                 return c2_fom_tick_generic(fom);
 *         else if (fom->fo_phase == FOO_PHASE_0) {
 *                 ...
 *                 if (!ready)
 *                         c2_fom_wait_on(fom, chan, &fom->fo_cb);
 *                 return ready ? C2_FSO_AGAIN : C2_FSO_WAIT;
 *         } else if (fom->fo_phase == FOO_PHASE_1) {
 *                 ...
 *         } else if (fom->fo_phase == FOO_PHASE_2) {
 *                 ...
 *         } else
 *                 C2_IMPOSSIBLE("Wrong phase.");
 * }
 * @endcode
 *
 * @see fom_long_lock.h for a higher level fom synchronisation mechanism.
 *
 * <b>Concurrency</b>
 *
 * The following types of activity are associated with a fom:
 *
 *     - fom phase transition function: c2_fom_ops::fo_tick()
 *
 *     - "top-half" of a call-back armed for a fom: c2_fom_callback::fc_top()
 *
 *     - "bottom-half" of a call-back armed for a fom:
 *       c2_fom_callback::fc_bottom()
 *
 * Phase transitions and bottom-halves are serialised: neither 2 phase
 * transitions, nor 2 bottom-halves, nor state transition and bottom-halve can
 * execute concurrently. Top-halves are not serialised and, moreover, executed
 * in an "awkward context": they are not allowed to block or take locks. It is
 * best to avoid using top-halves whenever possible.
 *
 * If a bottom-half becomes ready to execute for a fom in READY or RUNNING
 * state, the bottom-half remains pending until the fom goes into WAITING
 * state. Similarly, if c2_fom_wakeup() is called for a non-WAITING fom, the
 * wakeup remains pending until the fom moves into WAITING state.
 *
 * Fom-type-specific code should make no assumptions about request handler
 * threading model. Specifically, it is possible that phase transitions and
 * call-back halves for the same fom are executed by different
 * threads. In addition, no assumptions should be made about concurrency of
 * call-backs and phase transitions of *different* foms.
 *
 * <b>Blocking phase transitions</b>
 *
 * Sometimes the non-blockingness requirement for phase transitions is difficult
 * to satisfy, for example, if a fom has to call some external blocking code,
 * like db5. In these situations, phase transition function must notify request
 * handler that it is about to block the thread executing the phase
 * transition. This achieved by c2_fom_block_enter() call. Matching
 * c2_fom_block_leave() call notifies request handler that phase transition is
 * no longer blocked. These calls do not nest.
 *
 * Internally, c2_fom_block_enter() call hijacks the current request handler
 * thread into exclusive use by this fom. The fom remains in RUNNING state while
 * blocked. The code, executed between c2_fom_block_enter() and
 * c2_fom_block_leave() can arm call-backs, their bottom-halves won't be
 * executed until phase transition completes and fom returns back to WAITING
 * state. Similarly, a c2_fom_wakeup() wakeup posted for a blocked fom, remains
 * pending until fom moves into WAITING state. In other words, concurrency
 * guarantees listed in the "Concurrency" section are upheld for blocking phase
 * transitions.
 *
 * <b>Locality</b>
 *
 * Request handler partitions resources into "localities" to improve resource
 * utilisation by increasing locality of reference. A locality, represented by
 * c2_fom_locality owns a processor core and some other resources. Each locality
 * runs its own fom scheduler and maintains lists of ready and waiting foms. A
 * fom is assigned its "home" locality when it is created
 * (c2_fom_ops::fo_home_locality()).
 *
 * @see https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMzV6NzJfMTNkOGNjZmdnYg
 *
 * @todo describe intended fom and reqh usage on client.
 *
 * @{
 */

/* import */

#include "lib/queue.h"
#include "lib/thread.h"
#include "lib/bitmap.h"
#include "lib/mutex.h"
#include "lib/chan.h"
#include "lib/atomic.h"
#include "lib/tlist.h"

#include "fol/fol.h"
#include "stob/stob.h"
#include "reqh/reqh_service.h"

/* export */
struct c2_fom_domain;
struct c2_fom_domain_ops;
struct c2_fom_locality;
struct c2_fom_type;
struct c2_fom_type_ops;
struct c2_fom;
struct c2_fom_ops;
struct c2_long_lock;

/* defined in fom.c */
struct c2_loc_thread;

/**
 * A locality is a partition of computational resources dedicated to fom
 * execution on the node.
 *
 * Resources allotted to a locality are:
 *
 * - fraction of processor cycle bandwidth;
 *
 * - a collection of processors (or cores);
 *
 * - part of primary store.
 *
 * Once the locality is initialised, the locality invariant,
 * should hold true until locality is finalised.
 *
 * @see c2_locality_invaraint()
 */
struct c2_fom_locality {
	struct c2_fom_domain        *fl_dom;

	/** Run-queue */
	struct c2_list               fl_runq;
	size_t			     fl_runq_nr;

	/** Wait list */
	struct c2_list		     fl_wail;
	size_t			     fl_wail_nr;

	/** State Machine (SM) group for AST call-backs */
	struct c2_sm_group	     fl_group;

	/**
	 *  Re-scheduling channel that the handler thread waits on for new work.
	 *
	 *  @see http://www.tom-yam.or.jp/2238/src/slp.c.html#line2142 for
	 *  the explanation of the name.
	 */
	struct c2_chan		     fl_runrun;
	/**
	 * Set to true when the locality is finalised. This signals locality
	 * threads to exit.
	 */
	bool                         fl_shutdown;
	/** Handler thread */
	struct c2_loc_thread        *fl_handler;
	/** Idle threads */
	struct c2_tl                 fl_threads;
	struct c2_atomic64           fl_unblocking;
	struct c2_chan               fl_idle;

	/** Resources allotted to the partition */
	struct c2_bitmap	     fl_processors;

	/** Something for memory, see set_mempolicy(2). */
};

/**
 * Iterates over c2_fom_locality members and checks if
 * they are intialised and consistent.
 * This function must be invoked with c2_fom_locality::fl_group.s_lock
 * mutex held.
 */
bool c2_locality_invariant(const struct c2_fom_locality *loc);

/**
 * Domain is a collection of localities that compete for the resources.
 *
 * Once the fom domain is initialised, fom domain invariant should hold
 * true until fom domain is finalised .
 *
 * @see c2_fom_domain_invariant()
 */
struct c2_fom_domain {
	/** An array of localities. */
	struct c2_fom_locality		*fd_localities;
	/** Number of localities in the domain. */
	size_t				 fd_localities_nr;
	/** Number of foms under execution in this fom domain. */
	struct c2_atomic64               fd_foms_nr;
	/** Domain operations. */
	const struct c2_fom_domain_ops	*fd_ops;
	/** Request handler this domain belongs to */
	struct c2_reqh			*fd_reqh;
	/** Addb context for fom */
	struct c2_addb_ctx               fd_addb_ctx;
};

/** Operations vector attached to a domain. */
struct c2_fom_domain_ops {
	/**
	 *  Returns true if waiting (C2_FOS_WAITING) fom timed out and should be
	 *  moved into C2_FOPH_TIMEOUT phase.
	 *  @todo fom timeout implementation.
	 */
	bool   (*fdo_time_is_out)(const struct c2_fom_domain *dom,
				  const struct c2_fom *fom);
};

/**
 * States a fom can be in.
 */
enum c2_fom_state {
	C2_FOS_INIT,
	/** Fom is dequeued from wait queue and put in run queue. */
	C2_FOS_READY,
	/**
	 * Fom state transition function is being executed by a locality handler
	 * thread.  The fom is not on any queue in this state.
	 */
	C2_FOS_RUNNING,
	/** FOM is enqueued into a locality wait list.
	 */
	C2_FOS_WAITING,
	C2_FOS_FINISH,
};

enum c2_fom_phase {
	C2_FOM_PHASE_INIT,   /*< fom has been initialised. */
	C2_FOM_PHASE_FINISH, /*< terminal phase. */
	C2_FOM_PHASE_NR
};


/**
 * Initialises c2_fom_domain object provided by the caller.
 * Creates and initialises localities with handler threads.
 *
 * @param dom, fom domain to be initialised, provided by caller
 *
 * @pre dom != NULL
 */
int c2_fom_domain_init(struct c2_fom_domain *dom);

/**
 * Finalises fom domain.
 * Also finalises the localities in fom domain and destroys
 * the handler threads per locality.
 *
 * @param dom, fom domain to be finalised, all the
 *
 * @pre dom != NULL && dom->fd_localities != NULL
 */
void c2_fom_domain_fini(struct c2_fom_domain *dom);

/**
 * This function iterates over c2_fom_domain members and checks
 * if they are intialised.
 */
bool c2_fom_domain_invariant(const struct c2_fom_domain *dom);

/**
 * Fom call-back states
 */
enum c2_fc_state {
	C2_FCS_ARMED = 1,       /**< Armed */
	C2_FCS_TOP_DONE,	/**< Top-half done */
	C2_FCS_DONE,		/**< Bottom-half done */
};

/**
 * Represents a call-back to be executed when some event of fom's interest
 * happens.
 */
struct c2_fom_callback {
	/**
	 * This clink is registered with the channel where the event will be
	 * announced.
	 */
	struct c2_clink   fc_clink;
	/**
	 * AST to execute the call-back.
	 */
	struct c2_sm_ast  fc_ast;
	/**
	 * State, from enum c2_fc_state. int64_t is needed for
	 * c2_atomic64_cas().
	 */
	int64_t           fc_state;
	struct c2_fom    *fc_fom;
	/**
	 * Optional filter function executed from the clink call-back
	 * to filter out some events. This is top half of call-back.
	 * It can be executed concurrently with fom phase transition function.
	 */
	bool (*fc_top)(struct c2_fom_callback *cb);
	/**
	 * The bottom half of call-back. Never executed concurrently with the
	 * fom phase transition function.
	 */
	void (*fc_bottom)(struct c2_fom_callback *cb);
};

/**
 * Fop state machine.
 *
 * Once the fom is initialised, fom invariant,
 * should hold true as fom execution enters various
 * phases, including before fom is finalised.
 *
 * c2_fom is usually embedded into an ambient fom-type-specific object allocated
 * by c2_fom_type_ops::fto_create() or other means.
 *
 * c2_fom is geared towards supporting foms executing file operation requests
 * (fops), but can be used for any kind of server activity.
 *
 * @see c2_fom_invariant()
 */
struct c2_fom {
	/** Locality this fom belongs to */
	struct c2_fom_locality	 *fo_loc;
	struct c2_fom_type	 *fo_type;
	const struct c2_fom_ops	 *fo_ops;
	/** AST call-back to wake up the FOM */
	struct c2_fom_callback	  fo_cb;
	/** Request fop object, this fom belongs to */
	struct c2_fop		 *fo_fop;
	/** Reply fop object */
	struct c2_fop		 *fo_rep_fop;
	/** Fol object for this fom */
	struct c2_fol		 *fo_fol;
	/** Transaction object to be used by this fom */
	struct c2_dtx		  fo_tx;
	/** Pointer to service instance. */
	struct c2_reqh_service   *fo_service;
	/**
	 *  FOM linkage in the locality runq list or wait list
	 *  Every access to the FOM via this linkage is
	 *  protected by the c2_fom_locality::fl_group.s_lock mutex.
	 */
	struct c2_list_link	  fo_linkage;
	/** Transitions counter, coresponds to the number of
	    c2_fom_ops::fo_state() calls */
	unsigned		  fo_transitions;
	/** Counter of transitions, used to ensure FOM was inactive,
	    while waiting for a longlock. */
	unsigned		  fo_transitions_saved;

	/** State machine for generic and specfic FOM phases. */
	struct c2_sm		 fo_sm_phase;
	/** State machine for FOM states. */
	struct c2_sm		 fo_sm_state;
	/** Next FOM phase to be executed. */
	int			 fo_phase;
	/** Result of fom execution, -errno on failure */
	int32_t			  fo_rc;
	/** Thread executing current phase transition. */
	struct c2_loc_thread     *fo_thread;
	/**
	 * Stack of pending call-backs.
	 */
	struct c2_fom_callback   *fo_pending;
};

/**
 * Queues a fom for the execution in a locality runq.
 * Increments the number of foms in execution (c2_fom_domain::fd_foms_nr)
 * in fom domain atomically.
 * The fom is placed in the locality run-queue and scheduled for the execution.
 * Possible errors are reported through fom state and phase, hence the return
 * type is void.
 *
 * @param fom, A fom to be submitted for execution
 * @param reqh, request handler processing the fom given fop
 *
 * @pre is_locked(fom)
 * @pre fom->fo_phase == C2_FOM_PHASE_INIT
 */
void c2_fom_queue(struct c2_fom *fom, struct c2_reqh *reqh);

/**
 * Initialises fom allocated by caller.
 *
 * Invoked from c2_fom_type_ops::fto_create implementation for corresponding
 * fom.
 *
 * Fom starts in C2_FOM_PHASE_INIT phase and C2_FOS_RUNNING state to begin its
 * execution.
 *
 * @param fom A fom to be initialized
 * @param fom_type Fom type
 * @param ops Fom operations structure
 * @param fop Request fop object
 * @param reply Reply fop object
 * @pre fom != NULL
 */
void c2_fom_init(struct c2_fom *fom, struct c2_fom_type *fom_type,
		 const struct c2_fom_ops *ops, struct c2_fop *fop,
		 struct c2_fop *reply);
/**
 * Finalises a fom after it completes its execution,
 * i.e success or failure.
 * Also decrements the number of foms under execution in fom domain
 * atomically.
 * Also signals c2_reqh::rh_sd_signal once c2_fom_domain::fd_foms_nr
 * reaches 0.
 *
 * @param fom, A fom to be finalised
 * @pre fom->fo_phase == C2_FOM_PHASE_FINISH
*/
void c2_fom_fini(struct c2_fom *fom);

/**
 * Iterates over c2_fom members and check if they are consistent,
 * and also checks if the fom resides on correct list (i.e runq or
 * wait list) of the locality at any given instance.
 * This function must be invoked with c2_fom_locality::fl_group.s_lock
 * mutex held.
 */
bool c2_fom_invariant(const struct c2_fom *fom);

/** Type of fom. c2_fom_type is part of c2_fop_type. */
struct c2_fom_type {
	const struct c2_fom_type_ops *ft_ops;
	struct c2_sm_conf             ft_conf;
	/** It points to either generic SM phases or combined generic and
	 * specific phases.
	 */
	struct c2_sm_state_descr     *ft_phases;
	uint32_t                      ft_phases_nr;
	/** Service type this FOM type belongs to. */
	struct c2_reqh_service_type  *ft_rstype;
};

/**
 * Potential outcome of a fom phase transition.
 *
 * @see c2_fom_ops::fo_tick().
 */
enum c2_fom_phase_outcome {
	/**
	 *  Phase transition completed. The next phase transition would be
	 *  possible when some future event happens.
	 *
	 *  When C2_FSO_WAIT is returned, the fom is put on locality wait-list.
	 */
	C2_FSO_WAIT = 1,
	/**
	 * Phase transition completed and another phase transition is
	 * immediately possible.
	 *
	 * When C2_FSO_AGAIN is returned, either the next phase transition is
	 * immediately executed (by the same or by a different handler thread)
	 * or the fom is placed in the run-queue, depending on the scheduling
	 * constraints.
	 */
	C2_FSO_AGAIN,
};

/** Fom type operation vector. */
struct c2_fom_type_ops {
	/** Create a new fom for the given fop. */
	int (*fto_create)(struct c2_fop *fop, struct c2_fom **out);
};

/** Fom operations vector. */
struct c2_fom_ops {
	/** Finalise this fom. */
	void (*fo_fini)(struct c2_fom *fom);
	/**
	 *  Executes the next phase transition, "ticking" the fom machine.
	 *
	 *  Returns value of enum c2_fom_phase_outcome.
	 */
	int  (*fo_tick)(struct c2_fom *fom);

	/**
	 *  Finds home locality for this fom.
	 *
	 *  Returns numerical value based on certain fom parameters that is
	 *  used to select the home locality from c2_fom_domain::fd_localities
	 *  array.
	 */
	size_t  (*fo_home_locality) (const struct c2_fom *fom);
};

/**
 * This function is called before potential blocking point.
 *
 * @param fom, A fom executing a possible blocking operation
 * @see c2_fom_locality
 */
void c2_fom_block_enter(struct c2_fom *fom);

/**
 * This function is called after potential blocking point.
 *
 * @param fom, A fom done executing a blocking operation
 */
void c2_fom_block_leave(struct c2_fom *fom);

/**
 * Dequeues fom from the locality waiting queue and enqueues it into
 * locality runq list changing the state to C2_FOS_READY.
 *
 * @pre fom->fo_state == C2_FOS_WAITING
 * @pre is_locked(fom)
 * @param fom Ready to be executed fom, is put on locality runq
 */
void c2_fom_ready(struct c2_fom *fom);

/**
 * Moves the fom from waiting to ready queue. Similar to c2_fom_ready(), but
 * callable from a locality different from fom's locality (i.e., with a
 * different locality group lock held).
 */
void c2_fom_wakeup(struct c2_fom *fom);

/**
 * Initialises the call-back structure.
 */
void c2_fom_callback_init(struct c2_fom_callback *cb);

/**
 * Registers AST call-back with the channel and a fom executing a blocking
 * operation. Both, the channel and the call-back (with initialized fc_bottom)
 * are provided by user.
 * Callback will be called with locality lock held.
 *
 * @param fom, A fom executing a blocking operation
 * @param chan, waiting channel registered with the fom during its
 *              blocking operation
 * @param cb, AST call-back with initialized fc_bottom
 *            @see sm/sm.h
 */
void c2_fom_callback_arm(struct c2_fom *fom, struct c2_chan *chan,
                         struct c2_fom_callback *cb);

/**
 * The same as c2_fom_callback_arm(), but fc_bottom is initialized
 * automatically with internal static routine which just wakes up the fom.
 * Convenient when there is no need for custom fc_bottom.
 *
 * @param fom, A fom executing a blocking operation
 * @param chan, waiting channel registered with the fom during its
 *              blocking operation
 * @param cb, AST call-back
 *            @see sm/sm.h
  */
void c2_fom_wait_on(struct c2_fom *fom, struct c2_chan *chan,
                    struct c2_fom_callback *cb);

/**
 * Finalises the call-back. This is only safe to be called when:
 *
 *     - the call-back was never armed, or
 *
 *     - the last arming completely finished (both top- and bottom- halves were
 *       executed) or,
 *
 *     - the call-back was armed and the call to c2_fom_callback_cancel()
 *       returned true.
 */
void c2_fom_callback_fini(struct c2_fom_callback *cb);

/**
 * Attempts to cancel a pending call-back.
 *
 * @return true iff the call-back was successfully cancelled. It is guaranteed
 * that no call-back halves will be called after this point.
 *
 * @return false if it is too late to cancel a call-back.
 */
bool c2_fom_callback_cancel(struct c2_fom_callback *cb);

extern const struct c2_addb_loc c2_fom_addb_loc;
extern const struct c2_addb_ctx_type c2_fom_addb_ctx_type;

#define FOM_ADDB_ADD(fom, name, rc)  \
C2_ADDB_ADD(&(fom)->fo_fop->f_addb, &c2_fom_addb_loc, c2_addb_func_fail, \
	    (name), (rc))

/**
 * Returns the state of SM group for AST call-backs of locality, given fom is
 * associated with.
 */
bool c2_fom_group_is_locked(const struct c2_fom *fom);

#define C2_FOM_TYPE_DECLARE(fomt, ops, stype, phases) \
struct c2_fom_type fomt ## _fomt = {                  \
	.ft_ops = (ops),                              \
	.ft_rstype = (stype),                         \
	.ft_phases = (phases),                        \
	.ft_phases_nr = ARRAY_SIZE(phases),           \
}                                                     \

/** @} end of fom group */
/* __COLIBRI_FOP_FOM_H__ */
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
