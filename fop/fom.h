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
 * Original author: Original author: Nikita Danilov <nikita_danilov@xyratex.com>,
 *				     Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/04/2011
 */

#ifndef __COLIBRI_FOP_FOM_H__
#define __COLIBRI_FOP_FOM_H__

/**
   @defgroup fom Fop state Machine

   <b>Fop state machine (fom)</b>

   Fop state machine executes the fop. In addition to fop fields (which are file
   operation parameters), fom stores all the intermediate state necessary for
   the fop execution.

   A fom is not associated with any particular thread: each state transition is
   executed in the context of a certain handler thread, but the next state
   transition can be executed by a different thread.

   The aim of interfaces defined below is to simplify construction of a
   non-blocking file server (see HLD referenced below for a more detailed
   exposition).

   @see https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMzV6NzJfMTNkOGNjZmdnYg

   @{
 */

/* import */

#include "lib/queue.h"
#include "lib/thread.h"
#include "lib/bitmap.h"
#include "lib/mutex.h"
#include "lib/chan.h"
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
   A locality is a partition of computational resources dedicated to fom
   execution on the node.

   Resources allotted to a locality are:

   - fraction of processor cycle bandwidth;

   - a collection of processors (or cores);

   - part of primary store.

   Lock ordering:

   - runq list and wait list are protect by their respective locks
     i.e fl_runq_lock and fl_wail_lock.
   - Rest of the locality members are protected by locality's fl_lock.
   - No lock ordering is needed here as there's no conatiner relationship
     among locality members.

   Once the locality is initialised, the locality invariant,
   should hold true until locality is finalised.

   @see c2_locality_invaraint(struct c2_fom_locality *loc)
 */

struct c2_fom_locality {
	struct c2_fom_domain        *fl_dom;

	/** run-queue */
	struct c2_list               fl_runq;
	size_t			     fl_runq_nr;
	struct c2_mutex		     fl_runq_lock;

	/** wait list */
	struct c2_list		     fl_wail;
	size_t			     fl_wail_nr;
	struct c2_mutex		     fl_wail_lock;

	/** Lock for the locality fields not protected by the locks above. */
	struct c2_mutex		     fl_lock;

	/**
	    Re-scheduling channel that idle threads of locality wait on for new
	    work.

	    @see http://www.tom-yam.or.jp/2238/src/slp.c.html#line2142 for
	    the explanation of the name.
	*/
	struct c2_chan		     fl_runrun;

	/** handler threads */
	struct c2_list		     fl_threads;
	size_t			     fl_idle_threads_nr;
	size_t			     fl_threads_nr;

	/**
	    Minimum number of idle threads, that should be present in a
	    locality.
	 */
	size_t			     fl_lo_idle_threads_nr;

	/**
	    Maximum number of idle threads, that should be present in a
	    locality.
	 */
	size_t			     fl_hi_idle_threads_nr;

	/** Resources allotted to the partition */
	struct c2_bitmap	     fl_processors;

	/** Something for memory, see set_mempolicy(2). */
};

/**
   Domain is a collection of localities that compete for the resources. For
   example, there would be typically a domain for each service (c2_service).

   Once the fom domain is initialised, fom domain invariant should hold
   true until fom domain is finalised .

   @see c2_fom_domain_invariant(struct c2_fom_domain *dom)
 */
struct c2_fom_domain {
	/** An array of localities. */
	struct c2_fom_locality	       	*fd_localities;
	/** Number of localities in the domain. */
	size_t				fd_localities_nr;
	/** Domain operations. */
	const struct c2_fom_domain_ops 	*fd_ops;
	/** Request handler this domain belongs to */
	struct c2_reqh			*fd_reqh; 		
};

/** Operations vector attached to a domain. */
struct c2_fom_domain_ops {
	/** Returns true if waiting (FOS_WAITING) fom timed out and should be
	    moved into FOPH_TIMEOUT phase.
	    @todo fom timeout implementation.
	*/
	bool   (*fdo_time_is_out)(const struct c2_fom_domain *dom,
				  const struct c2_fom *fom);
};

/**
   States a fom can be in.

   A fom is in FOS_READY state when it is initialised and ready to
   be put on locality runq list for execution.
   A fom changes its state from FOS_READY to FOS_RUNNING, after it
   is dequeued from locality runq and begins its execution.
   A fom changes its state from FOS_RUNNING to FOS_WAITING, if its
   execution would block, fom is then put on locality wait list.
 */
enum c2_fom_state {
	/** The fom is being executed by a handler thread in some locality. */
	FOS_RUNNING,
	/** The fom is in the run-queue of some locality. */
	FOS_READY,
	/** The fom is in the wait-list of some locality. */
	FOS_WAITING,
};

/**
   "Phases" through which fom execution typically passes.

   This enumerates standard phases, handled by the generic code independent of
   fom type.

   @see https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjA2Zmc0N3I3Z2Y
 */
enum c2_fom_phase {
	FOPH_INIT,                  /*< fom has been initialised. */
	FOPH_AUTHENTICATE,          /*< authentication loop is in progress. */
	FOPH_AUTHENTICATE_WAIT,     /*< waiting for key cache miss. */
	FOPH_RESOURCE_LOCAL,        /*< local resource reservation loop is in
				      progress. */
	FOPH_RESOURCE_LOCAL_WAIT,   /*< waiting for a local resource. */
	FOPH_RESOURCE_DISTRIBUTED,  /*< distributed resource reservation loop is
				      in progress. */
	FOPH_RESOURCE_DISTRIBUTED_WAIT, /*< waiting for a distributed
					  resource. */
	FOPH_OBJECT_CHECK,          /*< object checking loop is in progress. */
	FOPH_OBJECT_CHECK_WAIT,     /*< waiting for object cache miss. */
	FOPH_AUTHORISATION,         /*< authorisation loop is in progress. */
	FOPH_AUTHORISATION_WAIT,    /*< waiting for userdb cache miss. */
	FOPH_TXN_CONTEXT,           /*< creating local transactional context. */
	FOPH_TXN_CONTEXT_WAIT,      /*< waiting for log space. */

	FOPH_QUEUE_REPLY,           /*< queuing reply fop-s. */
	FOPH_QUEUE_REPLY_WAIT,      /*< waiting for fop cache space. */
	FOPH_TXN_COMMIT,	    /*< commit local transaction context. */
	FOPH_TXN_COMMIT_WAIT,	    /*< waiting to commit local transaction context. */
	FOPH_TXN_ABORT,		    /*< abort local transaction context. */
	FOPH_TXN_ABORT_WAIT,	    /*< waiting to abort local transaction context. */
	FOPH_TIMEOUT,               /*< fom timed out. */
	FOPH_SUCCEED,		    /*< fom success. */
	FOPH_FAILURE,                /*< fom failed. */
	FOPH_DONE,		    /*< fom succeeded. */
	FOPH_NR                     /*< number of standard phases. fom type
				      specific phases have numbers larger than
				      this. */
};

/**

   Function initialises c2_fom_domain object provided by the caller.

   @pre dom != NULL
 */
int  c2_fom_domain_init(struct c2_fom_domain *dom);

/**
   Finalises fom domain.

   @pre dom != NULL && dom->fd_localities != NULL.
 */
void c2_fom_domain_fini(struct c2_fom_domain *dom);

/**
   Fop state machine.

   Once the fom is initialised, fom invariant,
   should hold true as fom execution enters various
   phases, including before fom is finalised.

   @see c2_fom_invariant(struct c2_fom *fom)
*/
struct c2_fom {
	enum c2_fom_state	 fo_state;
	int			 fo_phase;
	struct c2_fom_locality	*fo_loc;
	struct c2_fom_type	*fo_type;
	const struct c2_fom_ops	*fo_ops;
	struct c2_clink		 fo_clink;
	/** FOP ctx sent by the network service. */
	struct c2_fop_ctx	*fo_fop_ctx;
	/** request fop object, this fom belongs to */
	struct c2_fop		*fo_fop;
	/** reply fop object */
	struct c2_fop		*fo_rep_fop;
	/** fol object for this fom */
	struct c2_fol		*fo_fol;
	/** stob domain this fom is operating on */
	struct c2_stob_domain	*fo_stdomain;
	/** fom domain this fom is working in */
	struct c2_fom_domain	*fo_domain;
	/** transaction object to be used by this fom */
	struct c2_dtx		 fo_tx;
	/** linkage in the locality runq list or wait list  */
	struct c2_list_link	 fo_rwlink;
	/**
	    temporary reference to reply fop provided by client.

	    @todo to be removed later post integration with
	    latest rpc layer.
	 */
	void			*fo_cookie;
};

/**
   Queues a fom for the execution in a domain.

   A home locality is selected for the fom. The fom is placed in the
   corresponding run-queue and scheduled for the execution.

   Possible errors are reported through fom state and phase, hence the return
   type is void.

   @pre fom->fo_phase == FOPH_INIT.
 */
void c2_fom_queue(struct c2_fom *fom);

/**
   Initialises fom.

   Pre allocated fom is provided which is initialised.

   Invoked c2_fop_type_ops::fto_fom_init implementation of
   corresponding fop.

   @pre fom != NULL
 */
int c2_fom_init(struct c2_fom *fom);

/**
   Finalizes fom.

   Finalizes other fom members.

   @pre fom->fo_phase == FOPH_DONE.
*/
void c2_fom_fini(struct c2_fom *fom);

/** Type of fom. c2_fom_type is part of c2_fop_type. */
struct c2_fom_type {
	const struct c2_fom_type_ops *ft_ops;
};

/**
   Potential outcome of a fom state transition.

   @see c2_fom_ops::fo_state().
 */
enum c2_fom_state_outcome {
	/**
	    State transition completed. The next state transition would be
	    possible when some future event happens. The state transition
	    function registeres the fom's clink with the channel where this
	    event will be signalled.

	    When FSO_WAIT is returned, the fom is put on locality wait-list.

	    @see c2_fom_block_at().
	 */
	FSO_WAIT,
	/**
	   State transition completed and another state transition is
	   immediately possible.

	   When FSO_AGAIN is returned, either the next state transition is
	   immediately executed (by the same or by a different handler thread)
	   or the fom is placed in the run-queue, depending on the scheduling
	   constraints.
	 */
	FSO_AGAIN,
};

/** Fom type operation vector. */
struct c2_fom_type_ops {
	/** Create a new fom of this type. */
	int (*fto_create)(struct c2_fom_type *t, struct c2_fom **out);
};

/** Fom operations vector. */
struct c2_fom_ops {
	/** Finalise this fom. */
	void (*fo_fini)(struct c2_fom *fom);
	/**
	    Execute the next state transition.

	    Returns value of enum c2_fom_state_outcome or error code.
	 */
	int  (*fo_state)(struct c2_fom *fom);

	/**
	    Finds home locality for this fom.

	    Returns locality number used as subscript in fd_localities
	    array, member of c2_fom_domain, based on fom parameters.
	 */
	size_t  (*fo_home_locality) (const struct c2_fom *fom);
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
   Checks whether the fom locality has "enough" idle threads. If not, additional
   threads are started to cope with possible blocking point.

   This function is called before potential blocking point.
 */
void c2_fom_block_enter(struct c2_fom *fom);

/**
   Checks if number of idle threads are more than the threashold value
   i.e fl_lo_idle_threads_nr, and broadcasts on the thread waiting channel
   i.e fl_runrun.
   Accordingly, the extra idle threads commit suicide.

   This function is called after potential blocking point.
 */
void c2_fom_block_leave(struct c2_fom *fom);

/**
   Registers fom with the channel, so that next fom's state transition would
   happen when the channel is signalled.

   @pre !c2_clink_is_armed(&fom->fo_clink).
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
