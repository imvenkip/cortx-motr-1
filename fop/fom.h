/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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

#ifndef __MERO_FOP_FOM_H__
#define __MERO_FOP_FOM_H__

/**
 * @defgroup fom Fop state Machine
 *
 * <b>Fop state machine (fom)</b>
 *
 * A fom is a non-blocking state machine. Almost all server-side Mero
 * activities are implemented as foms. Specifically, all file system operation
 * requests, received from clients are executed as foms (hence the name).
 *
 * Fom execution is controlled by request handler (reqh) which can be thought of
 * as a specialised scheduler. Request handler is similar to a typical OS kernel
 * scheduler: it maintains lists of ready (runnable) and waiting foms. Mero
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
 * m0_fom::fo_phase. Generic code in fom.c pays no attention to this field,
 * except for special M0_FOM_PHASE_INIT and M0_FOM_PHASE_FINISH values used to
 * control fom life-time. ->fo_phase value is interpreted by fom-type-specific
 * code. fom_generic.[ch] defines some "standard phases", that a typical fom
 * related to file operation processing passes through.
 *
 * Each phase transition should be non-blocking. When a fom cannot move to the
 * next phase immediately, it waits for an event that would make non-blocking
 * phase transition possible.
 *
 * Internally, request handler maintains, in addition to phase, a
 * m0_fom::fo_state field, recording fom state, which can be RUNNING
 * (M0_FOS_RUNNING), READY (M0_FOS_READY) and WAITING (M0_FOS_WAITING). A fom is
 * in RUNNING state, when its phase transition is currently being executed. A
 * fom is in READY state, when its phase transition can be executed immediately
 * and a fom is in WAITING state, when no phase transition can be executed
 * immediately.
 *
 * Request handler, according to some policy, selects a fom in READY state,
 * moves it to RUNNING state and calls its m0_fom_ops::fo_tick() function to
 * execute phase transition. This function has 2 possible return values:
 *
 *     - M0_FSO_AGAIN: more phase transitions are possible immediately. When
 *       this value is returned, request handler returns the fom back to the
 *       READY state and guarantees that m0_fom_ops::fo_tick() will be called
 *       eventually as determined by policy. The reason to return M0_FSO_AGAIN
 *       instead of immediately executing the next phase transition right within
 *       ->fo_tick() is to get request handler a better chance to optimise
 *       performance globally, by selecting the "best" READY fom;
 *
 *     - M0_FSO_WAIT: no phase transitions are possible at the moment. As a
 *       special case, if m0_fom_phase(fom) == M0_FOM_PHASE_FINISH, request
 *       handler destroys the fom, by calling its m0_fom_ops::fo_fini()
 *       method. Otherwise, the fom is placed in WAITING state.
 *
 * A fom moves WAITING to READY state by the following means:
 *
 *     - before returning M0_FSO_WAIT, its ->fo_tick() function can arrange a
 *       wakeup, by calling m0_fom_wait_on(fom, chan, cb). When chan is
 *       signalled, the fom is moved to READY state. More generally,
 *       m0_fom_callback_arm(fom, chan, cb) call arranges for an arbitrary
 *       call-back to be called when the chan is signalled. The call-back can
 *       wake up the fom by calling m0_fom_ready();
 *
 *     - a WAITING fom can be woken up by calling m0_fom_wakeup() function that
 *       moves it in READY state.
 *
 * These two methods should not be mixed: internally they use the same
 * data-structure m0_fom::fo_cb.fc_ast.
 *
 * Typical ->fo_tick() function looks like
 *
 * @code
 * static int foo_tick(struct m0_fom *fom)
 * {
 *         struct foo_fom *obj = container_of(fom, struct foo_fom, ff_base);
 *
 *         if (m0_fom_phase(fom) < M0_FOPH_NR)
 *                 return m0_fom_tick_generic(fom);
 *         else if (m0_fom_phase(fom) == FOO_PHASE_0) {
 *                 ...
 *                 if (!ready)
 *                         m0_fom_wait_on(fom, chan, &fom->fo_cb);
 *                 return ready ? M0_FSO_AGAIN : M0_FSO_WAIT;
 *         } else if (m0_fom_phase(fom) == FOO_PHASE_1) {
 *                 ...
 *         } else if (m0_fom_phase(fom) == FOO_PHASE_2) {
 *                 ...
 *         } else
 *                 M0_IMPOSSIBLE("Wrong phase.");
 * }
 * @endcode
 *
 * @see fom_long_lock.h for a higher level fom synchronisation mechanism.
 *
 * <b>Concurrency</b>
 *
 * The following types of activity are associated with a fom:
 *
 *     - fom phase transition function: m0_fom_ops::fo_tick()
 *
 *     - "top-half" of a call-back armed for a fom: m0_fom_callback::fc_top()
 *
 *     - "bottom-half" of a call-back armed for a fom:
 *       m0_fom_callback::fc_bottom()
 *
 * Phase transitions and bottom-halves are serialised: neither 2 phase
 * transitions, nor 2 bottom-halves, nor state transition and bottom-halve can
 * execute concurrently. Top-halves are not serialised and, moreover, executed
 * in an "awkward context": they are not allowed to block or take locks. It is
 * best to avoid using top-halves whenever possible.
 *
 * If a bottom-half becomes ready to execute for a fom in READY or RUNNING
 * state, the bottom-half remains pending until the fom goes into WAITING
 * state. Similarly, if m0_fom_wakeup() is called for a non-WAITING fom, the
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
 * transition. This is achieved by m0_fom_block_enter() call. Matching
 * m0_fom_block_leave() call notifies request handler that phase transition is
 * no longer blocked. These calls do not nest.
 *
 * Internally, m0_fom_block_enter() call hijacks the current request handler
 * thread into exclusive use by this fom. The fom remains in RUNNING state while
 * blocked. The code, executed between m0_fom_block_enter() and
 * m0_fom_block_leave() can arm call-backs, their bottom-halves won't be
 * executed until phase transition completes and fom returns back to WAITING
 * state. Similarly, a m0_fom_wakeup() wakeup posted for a blocked fom, remains
 * pending until fom moves into WAITING state. In other words, concurrency
 * guarantees listed in the "Concurrency" section are upheld for blocking phase
 * transitions.
 *
 * <b>Locality</b>
 *
 * Request handler partitions resources into "localities" to improve resource
 * utilisation by increasing locality of reference. A locality, represented by
 * m0_fom_locality owns a processor core and some other resources. Each locality
 * runs its own fom scheduler and maintains lists of ready and waiting foms. A
 * fom is assigned its "home" locality when it is created
 * (m0_fom_ops::fo_home_locality()).
 *
 * @see https://docs.google.com/a/xyratex.com/
Doc?docid=0AQaCw6YRYSVSZGZmMzV6NzJfMTNkOGNjZmdnYg
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
#include "dtm/dtm.h"           /* m0_dtx */
#include "stob/stob.h"
#include "reqh/reqh_service.h"

/* export */
struct m0_fom_domain;
struct m0_fom_domain_ops;
struct m0_fom_locality;
struct m0_fom_type;
struct m0_fom_type_ops;
struct m0_fom;
struct m0_fom_ops;
struct m0_long_lock;

/* defined in fom.c */
struct m0_loc_thread;

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
 * @see m0_locality_invaraint()
 */
struct m0_fom_locality {
	struct m0_fom_domain        *fl_dom;

	/** Run-queue */
	struct m0_tl		     fl_runq;
	size_t			     fl_runq_nr;

	/** Wait list */
	struct m0_tl		     fl_wail;
	size_t			     fl_wail_nr;

	/**
	 * Total number of active foms in this locality. Equals the length of
	 * runq plus the length of wail plus the number of M0_FOS_RUNNING foms
	 * in all locality threads.
	 */
	unsigned                     fl_foms;

	/** State Machine (SM) group for AST call-backs */
	struct m0_sm_group	     fl_group;

	/**
	 *  Re-scheduling channel that the handler thread waits on for new work.
	 *
	 *  @see http://www.tom-yam.or.jp/2238/src/slp.c.html#line2142 for
	 *  the explanation of the name.
	 */
	struct m0_chan		     fl_runrun;
	/**
	 * Set to true when the locality is finalised. This signals locality
	 * threads to exit.
	 */
	bool                         fl_shutdown;
	/** Handler thread */
	struct m0_loc_thread        *fl_handler;
	/** Idle threads */
	struct m0_tl                 fl_threads;
	struct m0_atomic64           fl_unblocking;
	struct m0_chan               fl_idle;

	/** Resources allotted to the partition */
	struct m0_bitmap	     fl_processors;

	struct m0_addb_ctx           fl_addb_ctx;
	/**
	   Accumulated run time of all foms. Is is updated from fom_exec()
	   along with m0_fom::fo_exec_time.
	 */
	struct m0_addb_counter       fl_stat_run_times;
	/**
	   Accumulated scheduling overhead of all foms. It is updated from
	   fom_dequeue() counting by m0_fom::fo_sched_epoch.
	 */
	struct m0_addb_counter       fl_stat_sched_wait_times;

	/** AST which triggers the posting of statistics */
	struct m0_sm_ast             fl_post_stats_ast;

	/** Something for memory, see set_mempolicy(2). */
};

/**
 * Iterates over m0_fom_locality members and checks if
 * they are intialised and consistent.
 * This function must be invoked with m0_fom_locality::fl_group.s_lock
 * mutex held.
 */
M0_INTERNAL bool m0_locality_invariant(const struct m0_fom_locality *loc);

/**
 * Triggers the posting of statistics
 */
M0_INTERNAL void m0_fom_locality_post_stats(struct m0_fom_locality *loc);

/**
 * Domain is a collection of localities that compete for the resources.
 *
 * Once the fom domain is initialised, fom domain invariant should hold
 * true until fom domain is finalised .
 *
 * @see m0_fom_domain_invariant()
 */
struct m0_fom_domain {
	/** An array of localities. */
	struct m0_fom_locality		*fd_localities;
	/** Number of localities in the domain. */
	size_t				 fd_localities_nr;
	/** Domain operations. */
	const struct m0_fom_domain_ops	*fd_ops;
	/** Request handler this domain belongs to */
	struct m0_reqh			*fd_reqh;
	/** Addb context for fom */
	struct m0_addb_ctx               fd_addb_ctx;
};

/** Operations vector attached to a domain. */
struct m0_fom_domain_ops {
	/**
	 *  Returns true if waiting (M0_FOS_WAITING) fom timed out and should be
	 *  moved into M0_FOPH_TIMEOUT phase.
	 *  @todo fom timeout implementation.
	 */
	bool   (*fdo_time_is_out)(const struct m0_fom_domain *dom,
				  const struct m0_fom *fom);
};

/**
 * States a fom can be in.
 */
enum m0_fom_state {
	M0_FOS_INIT,
	/** Fom is dequeued from wait queue and put in run queue. */
	M0_FOS_READY,
	/**
	 * Fom state transition function is being executed by a locality handler
	 * thread.  The fom is not on any queue in this state.
	 */
	M0_FOS_RUNNING,
	/** FOM is enqueued into a locality wait list. */
	M0_FOS_WAITING,
	M0_FOS_FINISH,
};

/** The number of fom state transitions (see fom_trans[] at fom.c). */
enum { M0_FOS_TRANS_NR = 8 };

/**
 * Histogram arguments for m0_addb_rt_fom_state_stats.
 * Define the macro with histogram arguments if desired.
 * e.g. #define M0_FOM_STATE_STATS_HIST_ARGS 100, 200, 500
 */
#undef M0_FOM_STATE_STATS_HIST_ARGS

#ifdef M0_FOM_STATE_STATS_HIST_ARGS
#define M0_FOM_STATE_STATS_HIST_ARGS2 0, M0_FOM_STATE_STATS_HIST_ARGS
#else
#define M0_FOM_STATE_STATS_HIST_ARGS2
#endif
enum {
	FOM_STATE_STATS_DATA_SZ =
		(sizeof(struct m0_addb_counter_data) +
		  ((M0_COUNT_PARAMS(M0_FOM_STATE_STATS_HIST_ARGS2) > 0 ?
		    M0_COUNT_PARAMS(M0_FOM_STATE_STATS_HIST_ARGS2) + 1 : 0) *
                   sizeof(uint64_t))) *
		M0_FOS_TRANS_NR
};

enum m0_fom_phase {
	M0_FOM_PHASE_INIT,   /*< fom has been initialised. */
	M0_FOM_PHASE_FINISH, /*< terminal phase. */
	M0_FOM_PHASE_NR
};


/**
 * Initialises m0_fom_domain object provided by the caller.
 * Creates and initialises localities with handler threads.
 *
 * @param dom fom domain to be initialised, provided by caller
 *
 * @pre dom != NULL
 */
M0_INTERNAL int m0_fom_domain_init(struct m0_fom_domain *dom);

/**
 * Finalises fom domain.
 * Also finalises the localities in fom domain and destroys
 * the handler threads per locality.
 *
 * @param dom fom domain to be finalised, all the
 *
 * @pre dom != NULL && dom->fd_localities != NULL
 */
M0_INTERNAL void m0_fom_domain_fini(struct m0_fom_domain *dom);

/**
 * True iff no locality in the domain has a fom to execute.
 *
 * This function is, by intention, racy. To guarantee that the domain is idle,
 * the caller must first guarantee that no new foms can be queued.
 */
M0_INTERNAL bool m0_fom_domain_is_idle(const struct m0_fom_domain *dom);

/**
 * This function iterates over m0_fom_domain members and checks
 * if they are intialised.
 */
M0_INTERNAL bool m0_fom_domain_invariant(const struct m0_fom_domain *dom);

/**
 * Fom call-back states
 */
enum m0_fc_state {
	M0_FCS_ARMED = 1,       /**< Armed */
	M0_FCS_DONE,		/**< Bottom-half done */
};

/**
 * Represents a call-back to be executed when some event of fom's interest
 * happens.
 */
struct m0_fom_callback {
	/**
	 * This clink is registered with the channel where the event will be
	 * announced.
	 */
	struct m0_clink   fc_clink;
	/**
	 * AST to execute the call-back.
	 */
	struct m0_sm_ast  fc_ast;
	enum m0_fc_state  fc_state;
	struct m0_fom    *fc_fom;
	/**
	 * Optional filter function executed from the clink call-back
	 * to filter out some events. This is top half of call-back.
	 * It can be executed concurrently with fom phase transition function.
	 */
	bool (*fc_top)(struct m0_fom_callback *cb);
	/**
	 * The bottom half of call-back. Never executed concurrently with the
	 * fom phase transition function.
	 */
	void (*fc_bottom)(struct m0_fom_callback *cb);
};

/**
 * Fop state machine.
 *
 * Once the fom is initialised, fom invariant,
 * should hold true as fom execution enters various
 * phases, including before fom is finalised.
 *
 * m0_fom is usually embedded into an ambient fom-type-specific object allocated
 * by m0_fom_type_ops::fto_create() or other means.
 *
 * m0_fom is geared towards supporting foms executing file operation requests
 * (fops), but can be used for any kind of server activity.
 *
 * @see m0_fom_invariant()
 */
struct m0_fom {
	/** Locality this fom belongs to */
	struct m0_fom_locality	 *fo_loc;
	const struct m0_fom_type *fo_type;
	const struct m0_fom_ops	 *fo_ops;
	/** AST call-back to wake up the FOM */
	struct m0_fom_callback	  fo_cb;
	/** Request fop object, this fom belongs to */
	struct m0_fop		 *fo_fop;
	/** Reply fop object */
	struct m0_fop		 *fo_rep_fop;
	/** Transaction object to be used by this fom */
	struct m0_dtx		  fo_tx;
	/**
	 *  Set when the fom is used to execute local operation,
	 *  e.g., undo or redo during recovery.
	 */
	bool			  fo_local;
	/** Pointer to service instance. */
	struct m0_reqh_service   *fo_service;
	/** ADDB context for this fom */
	struct m0_addb_ctx        fo_addb_ctx;
	/** Imported operational ADDB context placeholder */
	struct m0_addb_ctx        fo_imp_op_addb_ctx;
	/**
	 * Optional imported operational ADDB context pointer to
	 * m0_fom::fo_imp_op_addb_ctx (NULL if not initialized).
	 * Initialized via m0_fom_op_addb_ctx_import().
	 * Use as the last element in a context vector when posting
	 * ADDB records for the FOM.
	 */
	struct m0_addb_ctx       *fo_op_addb_ctx;
	/**
	 *  FOM linkage in the locality runq list or wait list
	 *  Every access to the FOM via this linkage is
	 *  protected by the m0_fom_locality::fl_group.s_lock mutex.
	 */
	struct m0_tlink		  fo_linkage;
	/** Transitions counter, coresponds to the number of
	    m0_fom_ops::fo_state() calls */
	unsigned		  fo_transitions;
	/** Counter of transitions, used to ensure FOM was inactive,
	    while waiting for a longlock. */
	unsigned		  fo_transitions_saved;

	/** State machine for generic and specfic FOM phases.
	    sm_rc contains result of fom execution, -errno on failure.
	 */
	struct m0_sm		  fo_sm_phase;
	/** State machine for FOM states. */
	struct m0_sm		  fo_sm_state;

	/** addb sm counter for states statistics */
	struct m0_addb_sm_counter fo_sm_state_stats;
	/** counter data for states statistics */
	uint8_t fo_fos_stats_data[FOM_STATE_STATS_DATA_SZ];

	/** Thread executing current phase transition. */
	struct m0_loc_thread     *fo_thread;
	/**
	 * Stack of pending call-backs.
	 */
	struct m0_fom_callback   *fo_pending;

	/** Schedule start epoch (used to calculate scheduling overhead). */
	m0_time_t		  fo_sched_epoch;

	uint64_t		  fo_magic;
};

/**
 * Queues a fom for the execution in a locality runq.
 *
 * Increments the number of foms in execution (m0_fom_locality::fl_foms).
 *
 * The fom is placed in the locality run-queue and scheduled for the execution.
 * Possible errors are reported through fom state and phase, hence the return
 * type is void.
 *
 * @param fom A fom to be submitted for execution
 * @param reqh request handler processing the fom
 *
 * @pre m0_fom_group_is_locked(fom)
 * @pre m0_fom_phase(fom) == M0_FOM_PHASE_INIT
 */
M0_INTERNAL void m0_fom_queue(struct m0_fom *fom, struct m0_reqh *reqh);

/**
 * Returns reqh the fom belongs to
 */
M0_INTERNAL struct m0_reqh *m0_fom_reqh(const struct m0_fom *fom);

/**
 * Initialises fom allocated by caller.
 *
 * Invoked from m0_fom_type_ops::fto_create implementation for corresponding
 * fom.
 *
 * Fom starts in M0_FOM_PHASE_INIT phase and M0_FOS_RUNNING state to begin its
 * execution.
 *
 * @param fom A fom to be initialized
 * @param fom_type Fom type
 * @param ops Fom operations structure
 * @param fop Request fop object
 * @param reply Reply fop object
 * @param reqh Request handler that will execute this fom
 * @param stype Service type that is used to get the service,
 *        which is required to set parent addb ctx for this fom
 * @pre fom != NULL
 * @pre reqh != NULL
 */
void m0_fom_init(struct m0_fom *fom, struct m0_fom_type *fom_type,
		 const struct m0_fom_ops *ops, struct m0_fop *fop,
		 struct m0_fop *reply, struct m0_reqh *reqh,
		 const struct m0_reqh_service_type *stype);
/**
 * Finalises a fom after it completes its execution,
 * i.e success or failure.
 *
 * Decrements the number of foms under execution in the locality
 * (m0_fom_locality::fl_foms). Signals m0_reqh::rh_sd_signal once this counter
 * reaches 0.
 *
 * @param fom A fom to be finalised
 * @pre m0_fom_phase(fom) == M0_FOM_PHASE_FINISH
*/
void m0_fom_fini(struct m0_fom *fom);

/**
 * Enables m0_sm state statistics for the fom's phases.
 * Must be called between m0_fom_init() and m0_fom_queue()
 * (which calls m0_fom_sm_init() inside).
 * It is caller's responsibility to:
 *
 *  - initialize the counter;
 *
 *  - serialize access to the counter between different state machines
 *    along with posting; for this reason it is encouraged that the counter
 *    is shared only among the state machines of the same group;
 *
 *  - finalize the counter.
 *
 * @param c counter initialized by caller.
 *
 * @pre fom != NULL
 * @pre fom->fo_sm_phase.sm_state_epoch == 0
 */
M0_INTERNAL void m0_fom_phase_stats_enable(struct m0_fom *fom,
					   struct m0_addb_sm_counter *c);

/**
 * Iterates over m0_fom members and check if they are consistent,
 * and also checks if the fom resides on correct list (i.e runq or
 * wait list) of the locality at any given instance.
 * This function must be invoked with m0_fom_locality::fl_group.s_lock
 * mutex held.
 */
M0_INTERNAL bool m0_fom_invariant(const struct m0_fom *fom);

/** Type of fom. m0_fom_type is part of m0_fop_type. */
struct m0_fom_type {
	const struct m0_fom_type_ops	  *ft_ops;
	const struct m0_sm_conf		  *ft_conf;
	const struct m0_reqh_service_type *ft_rstype;
};

/**
 * Potential outcome of a fom phase transition.
 *
 * @see m0_fom_ops::fo_tick().
 */
enum m0_fom_phase_outcome {
	/**
	 *  Phase transition completed. The next phase transition would be
	 *  possible when some future event happens.
	 *
	 *  When M0_FSO_WAIT is returned, the fom is put on locality wait-list.
	 */
	M0_FSO_WAIT = 1,
	/**
	 * Phase transition completed and another phase transition is
	 * immediately possible.
	 *
	 * When M0_FSO_AGAIN is returned, either the next phase transition is
	 * immediately executed (by the same or by a different handler thread)
	 * or the fom is placed in the run-queue, depending on the scheduling
	 * constraints.
	 */
	M0_FSO_AGAIN,
};

/** Fom type operation vector. */
struct m0_fom_type_ops {
	/** Create a new fom for the given fop. */
	int (*fto_create)(struct m0_fop *fop, struct m0_fom **out,
			  struct m0_reqh *reqh);
};

/** Fom operations vector. */
struct m0_fom_ops {
	/** Finalise this fom. */
	void (*fo_fini)(struct m0_fom *fom);
	/**
	 *  Executes the next phase transition, "ticking" the fom machine.
	 *
	 *  Returns value of enum m0_fom_phase_outcome.
	 */
	int  (*fo_tick)(struct m0_fom *fom);
	/**
	 *  Finds home locality for this fom.
	 *
	 *  Returns numerical value based on certain fom parameters that is
	 *  used to select the home locality from m0_fom_domain::fd_localities
	 *  array.
	 */
	size_t  (*fo_home_locality) (const struct m0_fom *fom);
	/** Initializes ADDB context of this fom, invoked by m0_fom_init() */
	void (*fo_addb_init)(struct m0_fom *fom, struct m0_addb_mc *mc);
};

/**
 * This function is called before potential blocking point.
 *
 * @param fom A fom executing a possible blocking operation
 * @see m0_fom_locality
 */
M0_INTERNAL void m0_fom_block_enter(struct m0_fom *fom);

/**
 * This function is called after potential blocking point.
 *
 * @param fom A fom done executing a blocking operation
 */
M0_INTERNAL void m0_fom_block_leave(struct m0_fom *fom);

/**
 * Dequeues fom from the locality waiting queue and enqueues it into
 * locality runq list changing the state to M0_FOS_READY.
 *
 * @pre fom->fo_state == M0_FOS_WAITING
 * @pre m0_fom_group_is_locked(fom)
 * @param fom Ready to be executed fom, is put on locality runq
 */
M0_INTERNAL void m0_fom_ready(struct m0_fom *fom);

/**
 * Moves the fom from waiting to ready queue. Similar to m0_fom_ready(), but
 * callable from a locality different from fom's locality (i.e., with a
 * different locality group lock held).
 */
M0_INTERNAL void m0_fom_wakeup(struct m0_fom *fom);

/**
 * Initialises the call-back structure.
 */
M0_INTERNAL void m0_fom_callback_init(struct m0_fom_callback *cb);

/**
 * Registers AST call-back with the channel and a fom executing a blocking
 * operation. Both, the channel and the call-back (with initialized fc_bottom)
 * are provided by user.
 * Callback will be called with locality lock held.
 *
 * @param fom A fom executing a blocking operation
 * @param chan waiting channel registered with the fom during its
 *              blocking operation
 * @param cb AST call-back with initialized fc_bottom
 *            @see sm/sm.h
 */
M0_INTERNAL void m0_fom_callback_arm(struct m0_fom *fom, struct m0_chan *chan,
				     struct m0_fom_callback *cb);

/**
 * The same as m0_fom_callback_arm(), but fc_bottom is initialized
 * automatically with internal static routine which just wakes up the fom.
 * Convenient when there is no need for custom fc_bottom.
 *
 * @param fom A fom executing a blocking operation
 * @param chan waiting channel registered with the fom during its
 *              blocking operation
 * @param cb AST call-back
 *            @see sm/sm.h
  */
M0_INTERNAL void m0_fom_wait_on(struct m0_fom *fom, struct m0_chan *chan,
				struct m0_fom_callback *cb);

/**
 * Finalises the call-back. This is only safe to be called when:
 *
 *     - the call-back was never armed, or
 *
 *     - the last arming completely finished (both top- and bottom- halves were
 *       executed) or,
 *
 *     - the call-back was armed and the call to m0_fom_callback_cancel()
 *       returned true.
 */
M0_INTERNAL void m0_fom_callback_fini(struct m0_fom_callback *cb);

/**
 * Cancels a pending call-back.
 *
 * It is guaranteed that call-back function won't be executing after this
 * function returns (either because it already completed, or because the
 * call-back was cancelled).
 */
M0_INTERNAL void m0_fom_callback_cancel(struct m0_fom_callback *cb);

/**
 * Fom timeout allows a user-supplied call-back to be executed under the fom's
 * locality's lock after a specified timeout.
 *
 * As a special case m0_fom_timeout_wait_on() wakes the fom after a specified
 * time-out. Compare with m0_fom_callback.
 */
struct m0_fom_timeout {
	/**
	 * State machine timer used to implement the timeout.
	 */
	struct m0_sm_timer     to_timer;
	/**
	 * Call-back structure used to deliver the call-back.
	 */
	struct m0_fom_callback to_cb;
};

M0_INTERNAL void m0_fom_timeout_init(struct m0_fom_timeout *to);
M0_INTERNAL void m0_fom_timeout_fini(struct m0_fom_timeout *to);

/**
 * Arranges for the fom to be woken up after a specified (absolute) timeout.
 *
 * This must be called under the locality lock. If called from a tick function,
 * the function should return M0_FSO_WAIT.
 *
 * @pre m0_fom_group_is_locked(fom)
 */
M0_INTERNAL int m0_fom_timeout_wait_on(struct m0_fom_timeout *to,
				       struct m0_fom *fom, m0_time_t deadline);
/**
 * Arranges for the given call-back "cb" to be called after the specified
 * (absolute) deadline.
 *
 * "cb" will be called with m0_fom_callback structure, containing pointer to the
 * fom in ->fc_fom field.
 *
 * @pre m0_fom_group_is_locked(fom)
 */
M0_INTERNAL int m0_fom_timeout_arm(struct m0_fom_timeout *to,
				   struct m0_fom *fom,
				   void (*cb)(struct m0_fom_callback *),
				   m0_time_t deadline);
/**
 * Attempts to cancel the fom timeout.
 *
 * The timer call-back won't be executing by the time this function returns.
 *
 * @pre m0_fom_group_is_locked(fom)
 */
M0_INTERNAL void m0_fom_timeout_cancel(struct m0_fom_timeout *to);

/**
 * Returns the state of SM group for AST call-backs of locality, given fom is
 * associated with.
 */
M0_INTERNAL bool m0_fom_group_is_locked(const struct m0_fom *fom);

/**
 * Initialises FOM state machines for phases and states.
 * @see m0_fom::fo_sm_phase
 * @see m0_fom::fo_sm_state
 *
 * @pre fom->fo_loc != NULL
 */
M0_INTERNAL void m0_fom_sm_init(struct m0_fom *fom);

void m0_fom_phase_set(struct m0_fom *fom, int phase);

void m0_fom_phase_move(struct m0_fom *fom, int32_t rc, int phase);

void m0_fom_phase_moveif(struct m0_fom *fom, int32_t rc, int phase0,
			 int phase1);

int m0_fom_phase(const struct m0_fom *fom);

M0_INTERNAL int m0_fom_rc(const struct m0_fom *fom);

M0_INTERNAL bool m0_fom_is_waiting(const struct m0_fom *fom);

M0_INTERNAL void m0_fom_type_init(struct m0_fom_type *type,
				  const struct m0_fom_type_ops *ops,
				  const struct m0_reqh_service_type *svc_type,
				  struct m0_sm_conf *sm);

/**
 * Associate an operational context with the FOM.
 *
 * Such a context is usually provided by the entity that triggered
 * the creation of the FOM.
 *
 * @param fom The FOM object pointer.
 * @param id Pointer to the context identifier sequence representing
 * the operational context. The memory associated with this sequence
 * must remain stable until the FOM is destroyed.
 */
M0_INTERNAL int m0_fom_op_addb_ctx_import(struct m0_fom *fom,
					const struct m0_addb_uint64_seq *id);

#define M0_FOM_ADDB_CTX_VEC(fom) ({					\
	typeof(fom) __fom = fom;					\
	M0_ADDB_CTX_VEC(&__fom->fo_addb_ctx, __fom->fo_op_addb_ctx);	\
})

/**
 * Helper macro for ADDB posting.
 */
#define M0_FOM_ADDB_POST(fom, addb_mc, recid, ...) \
	M0_ADDB_POST(addb_mc, recid, M0_FOM_ADDB_CTX_VEC(fom), ## __VA_ARGS__);

/** @} end of fom group */
/* __MERO_FOP_FOM_H__ */
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
