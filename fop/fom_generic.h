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
 * Original author: Mandar Sawant <Mandar_Sawant@xyratex.com>
 *		    Madhavrao Vemuri <madhav_vemuri@xyratex.com>
 * Original creation date: 08/08/2012
 */

/**
 * "Phases" through which fom execution typically passes.
 *
 * This enumerates standard phases, handled by the generic code independent of
 * fom type.
 *
 * @see https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjA2Zmc0N3I3Z2Y
 * @see c2_fom_state_transition()
 */
enum c2_fom_generic_phase {
	C2_FOPH_INIT,		     /*< fom has been initialised. */
	C2_FOPH_FINISH,		     /*< terminal state. */
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
	C2_FOPH_FOL_REC_ADD,        /*< add a FOL transaction record. */
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
	C2_FOPH_NR                  /*< number of standard phases. fom type
	                                specific phases have numbers larger than
	                                this. */
};

/**
 * Transitions through both standard and specific phases until C2_FSO_WAIT is
 * returned by a state function.
 * Each FOM phase method needs to either return next phase to transition into or
 * set fom->fo_next_phase and return C2_FSO_WAIT in case the FOM executes a
 * blocking function.
 *
 * @retval C2_FSO_WAIT, if FOM is blocking.
 */
int c2_fom_state_transition(struct c2_fom *fom);

/**
 * Initialises FOM state machines,
 * @see c2_fom::fo_sm_phase
 * @see c2_fom::fo_sm_state
 *
 * @pre c2_group_is_locked(fom)
 */
void c2_fom_sm_init(struct c2_fom *fom);

/**
 * Combines standard and specific FOM phases and returns
 * the resultant state machine configuration in fom_type->ft_conf.
 */
void c2_fom_type_register(struct c2_fom_type *fom_type);

extern const struct c2_sm_conf fom_conf;

/** Returns FOM from state machine c2_fom::fo_sm_phase. */
static inline struct c2_fom* c2_sm2fom(const struct c2_sm *sm)
{
	C2_PRE(sm != NULL);
	return container_of(sm, struct c2_fom, fo_sm_phase);
}

