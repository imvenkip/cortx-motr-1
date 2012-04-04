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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>,
 *		    Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/19/2010
 */

#ifndef __COLIBRI_FOP_FOM_H__
#define __COLIBRI_FOP_FOM_H__

/**
 * @defgroup fom Fop state Machine
 *
 * <b>Fop state machine (fom)</b>
 *
 * Fop state machine executes the fop. In addition to fop fields (which are file
 * operation parameters), fom stores all the intermediate state necessary for
 * the fop execution.
 *
 * A fom is not associated with any particular thread: each state transition is
 * executed in the context of a certain handler thread, but the next state
 * transition can be executed by a different thread.
 *
 * The aim of interfaces defined below is to simplify construction of a
 * non-blocking file server (see HLD referenced below for a more detailed
 * exposition).
 *
 * @see https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMzV6NzJfMTNkOGNjZmdnYg
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

#include "fol/fol.h"
#include "stob/stob.h"

struct c2_fop_type;

/* export */
struct c2_fom_domain;
struct c2_fom_domain_ops;
struct c2_fom_locality;
struct c2_fom_type;
struct c2_fom_type_ops;
struct c2_fom;
struct c2_fom_ops;

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
 * Lock ordering:
 *
 * - no lock ordering is needed here as access to all the locality members
 *   is protected by a common locality lock, c2_fom_locality::fl_lock.
 *   All the operations on locality members are performed independently using
 *   simple locking and unlocking semantics.
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

	/** Common lock used to protect locality fields */
	struct c2_mutex		     fl_lock;

	/**
	 *  Re-scheduling channel that idle threads of locality wait on for new
	 *  work.
	 * 
	 *  @see http://www.tom-yam.or.jp/2238/src/slp.c.html#line2142 for
	 *  the explanation of the name.
	 */
	struct c2_chan		     fl_runrun;

	/** Handler threads */
	struct c2_list		     fl_threads;
	size_t			     fl_idle_threads_nr;
	size_t			     fl_threads_nr;

	/**
	 *  Minimum number of idle threads, that should be present in a
	 *  locality.
	 */
	size_t			     fl_lo_idle_threads_nr;

	/**
	 *  Maximum number of idle threads, that should be present in a
	 *  locality.
	 */
	size_t			     fl_hi_idle_threads_nr;

	/** Resources allotted to the partition */
	struct c2_bitmap	     fl_processors;

	/** Something for memory, see set_mempolicy(2). */
};

/**
 * Iterates over c2_fom_locality members and checks if
 * they are intialised and consistent.
 * This function must be invoked with c2_fom_locality::fl_lock
 * mutex held.
 */
bool c2_locality_invariant(const struct c2_fom_locality *loc);

/**
 * Domain is a collection of localities that compete for the resources. For
 * example, there would be typically a domain for each service (c2_service).
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
	/**
	 * Fom is in C2_FOS_RUNNING state when its state transition function is
	 * being executed by a locality handler thread.  The fom is not on any
	 * queue in this state.
	 */
	C2_FOS_RUNNING,
	/**
	 * Fom is in C2_FOS_READY state when it is on locality runq for
	 * execution.
	 */
	C2_FOS_READY,
	/**
	 * Fom is in C2_FOS_WAITING state when some event must happen before
	 * the next state transition would become possible.  The fom is on a
	 * locality wait list in this state.
	 */
	C2_FOS_WAITING,
};

/**
 * "Phases" through which fom execution typically passes.
 *
 * This enumerates standard phases, handled by the generic code independent of
 * fom type.
 *
 * @see https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjA2Zmc0N3I3Z2Y
 * @see c2_fom_state_generic()
 */
enum c2_fom_phase {
	C2_FOPH_INIT,                /*< fom has been initialised. */
	C2_FOPH_AUTHENTICATE,        /*< authentication loop is in progress. */
	C2_FOPH_AUTHENTICATE_WAIT,   /*< waiting for key cache miss. */
	C2_FOPH_RESOURCE_LOCAL,      /*< local resource reservation loop is in
	                                 progress. */
	C2_FOPH_RESOURCE_LOCAL_WAIT, /*< waiting for a local resource. */
	C2_FOPH_RESOURCE_DISTRIBUTED,/*< distributed resource reservation loop
	                                 is in progress. */
	C2_FOPH_RESOURCE_DISTRIBUTED_WAIT, /*< waiting for a distributed
	                                       resource. */
	C2_FOPH_OBJECT_CHECK,       /*< object checking loop is in progress. */
	C2_FOPH_OBJECT_CHECK_WAIT,  /*< waiting for object cache miss. */
	C2_FOPH_AUTHORISATION,      /*< authorisation loop is in progress. */
	C2_FOPH_AUTHORISATION_WAIT, /*< waiting for userdb cache miss. */
	C2_FOPH_TXN_CONTEXT,        /*< creating local transactional context. */
	C2_FOPH_TXN_CONTEXT_WAIT,   /*< waiting for log space. */
	C2_FOPH_SUCCESS,            /*< fom execution completed succesfully. */
	C2_FOPH_TXN_COMMIT,         /*< commit local transaction context. */
	C2_FOPH_TXN_COMMIT_WAIT,    /*< waiting to commit local transaction
	                                context. */
	C2_FOPH_TIMEOUT,            /*< fom timed out. */
	C2_FOPH_FAILURE,            /*< fom execution failed. */
	C2_FOPH_TXN_ABORT,          /*< abort local transaction context. */
	C2_FOPH_TXN_ABORT_WAIT,	    /*< waiting to abort local transaction
	                                context. */
	C2_FOPH_QUEUE_REPLY,        /*< queuing fop reply.  */
	C2_FOPH_QUEUE_REPLY_WAIT,   /*< waiting for fop cache space. */
	C2_FOPH_FINISH,	            /*< terminal state. */
	C2_FOPH_NR                  /*< number of standard phases. fom type
	                                specific phases have numbers larger than
	                                this. */
};

/**
 * Initialises c2_fom_domain object provided by the caller.
 * Creates and initialises localities with handler threads.
 *
 * @param dom, fom domain to be initialised, provided by caller
 *
 * @pre dom != NULL
 */
int  c2_fom_domain_init(struct c2_fom_domain *dom);

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
 * Fop state machine.
 *
 * Once the fom is initialised, fom invariant,
 * should hold true as fom execution enters various
 * phases, including before fom is finalised.
 *
 * @see c2_fom_invariant()
 */
struct c2_fom {
	/**
	 * State a fom can be in at any given instance throughout its
	 * life cycle.This feild is protected by c2_fom_locality:fl_lock
	 * mutex, except in reqh handler thread, when a fom is dequeued
	 * from locality runq list for execution.
	 *
	 * @see c2_fom_locality
	 */
	enum c2_fom_state	 fo_state;
	/** FOM phase under execution */
	int			 fo_phase;
	/** Locality this fom belongs to */
	struct c2_fom_locality	*fo_loc;
	struct c2_fom_type	*fo_type;
	const struct c2_fom_ops	*fo_ops;
	/** FOM clink to wait upon a particular channel for an event */
	struct c2_clink		 fo_clink;
	/** FOP ctx sent by the network service. */
	struct c2_fop_ctx	*fo_fop_ctx;
	/** Request fop object, this fom belongs to */
	struct c2_fop		*fo_fop;
	/** Reply fop object */
	struct c2_fop		*fo_rep_fop;
	/** Fol object for this fom */
	struct c2_fol		*fo_fol;
	/** Transaction object to be used by this fom */
	struct c2_dtx		 fo_tx;
	/** Pointer to service instance. */
	struct c2_reqh_service  *fo_service;
	/**
	 *  FOM linkage in the locality runq list or wait list
	 *  Every access to the FOM via this linkage is
	 *  protected by the c2_fom_locality::fl_lock mutex.
	 */
	struct c2_list_link	 fo_linkage;

	/** Result of fom execution, -errno on failure */
	int32_t			 fo_rc;
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
 * @pre fom->fo_phase == C2_FOPH_INIT || fom->fo_phase == C2_FOPH_FAILURE
 */
void c2_fom_queue(struct c2_fom *fom);

/**
 * Initialises fom allocated by caller.
 *
 * Invoked from c2_fom_type_ops::fto_create implementation for corresponding
 * fom.
 *
 * Fom starts in C2_FOPH_INIT phase and C2_FOS_RUNNING state to begin its
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
 *
 * @param fom, A fom to be finalised
 * @pre fom->fo_phase == C2_FOPH_FINISH
*/
void c2_fom_fini(struct c2_fom *fom);

/**
 * Iterates over c2_fom members and check if they are consistent,
 * and also checks if the fom resides on correct list (i.e runq or
 * wait list) of the locality at any given instance.
 * This function must be invoked with c2_fom_locality::fl_lock
 * mutex held.
 */
bool c2_fom_invariant(const struct c2_fom *fom);

/** Type of fom. c2_fom_type is part of c2_fop_type. */
struct c2_fom_type {
	const struct c2_fom_type_ops *ft_ops;
};

/**
 * Potential outcome of a fom state transition.
 *
 * @see c2_fom_ops::fo_state().
 */
enum c2_fom_state_outcome {
	/**
	 *  State transition completed. The next state transition would be
	 *  possible when some future event happens. The state transition
	 *  function registeres the fom's clink with the channel where this
	 *  event will be signalled.
	 *
	 *  When C2_FSO_WAIT is returned, the fom is put on locality wait-list.
	 *
	 *  @see c2_fom_block_at().
	 */
	C2_FSO_WAIT,
	/**
	 * State transition completed and another state transition is
	 * immediately possible.
	 *
	 * When C2_FSO_AGAIN is returned, either the next state transition is
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
	 *  Executes the next state transition.
	 *
	 *  Returns value of enum c2_fom_state_outcome or error code.
	 */
	int  (*fo_state)(struct c2_fom *fom);

	/**
	 *  Finds home locality for this fom.
	 *
	 *  Returns numerical value based on certain fom parameters that is
	 *  used to select the home locality from c2_fom_domain::fd_localities
	 *  array.
	 */
	size_t  (*fo_home_locality) (const struct c2_fom *fom);

	/**
	 * Get service name which executes this fom.
	 */
	const char *(*fo_service_name) (struct c2_fom *fom);
};

/** Handler thread. */
struct c2_fom_hthread {
	struct c2_thread	fht_thread;
	/** Linkage into c2_fom_locality::fl_threads. */
	struct c2_list_link	fht_linkage;
	/** locality this thread belongs to */
	struct c2_fom_locality	*fht_locality;
};

/**
 * This function is called before potential blocking point.
 * Checks whether the fom locality has "enough" idle threads.
 * If not, additional threads are started to cope with possible
 * blocking point.
 * Increments c2_fom_locality::fl_lo_idle_threads_nr, so that
 * there exists atleast one idle thread to handle incoming fop if
 * the calling thread blocks.
 *
 * @param fom, A fom executing a possible blocking operation
 * @see c2_fom_locality
 */
void c2_fom_block_enter(struct c2_fom *fom);

/**
 * This function is called after potential blocking point.
 * Decrements c2_fom_locality::fl_lo_idle_threads_nr, so that
 * extra idle threads are destroyed automatically.
 *
 * @param fom, A fom done executing a blocking operation
 */
void c2_fom_block_leave(struct c2_fom *fom);

/**
 * Registers fom with the channel provided by the caller on which
 * the fom would wait for signal after completing a blocking operation.
 * This function returns with c2_fom_locality::fl_lock held.
 * Fom resumes its execution once the chan is signalled.
 *
 * @param fom, A fom executing a blocking operation
 * @param chan, waiting channel registered with the fom during its
 *              blocking operation
 * @pre !c2_clink_is_armed(&fom->fo_clink)
 */
void c2_fom_block_at(struct c2_fom *fom, struct c2_chan *chan);

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
