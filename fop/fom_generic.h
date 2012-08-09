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
 * Original creation date: 09-Aug-2012
 */

#pragma once

#ifndef __COLIBRI_FOP_FOM_GENERIC_H__
#define __COLIBRI_FOP_FOM_GENERIC_H__

#include "fop/fom.h"

/**
 * @addtogroup fom
 */

/**
 * "Phases" through which fom execution typically passes.
 *
 * This enumerates standard phases, handled by the generic code independent of
 * fom type.
 *
 * @see https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjA2Zmc0N3I3Z2Y
 * @see c2_fom_tick_generic()
 */
enum c2_fom_standard_phase {
	C2_FOPH_INIT = C2_FOM_PHASE_INIT,  /*< fom has been initialised. */
	C2_FOPH_FINISH = C2_FOM_PHASE_FINISH,  /*< terminal phase. */
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
   Standard fom phase transition function.

   This function handles standard fom phases from enum c2_fom_standard_phase.

   First do "standard actions":

   - authenticity checks: reqh verifies that protected state in the fop is
     authentic. Various bits of information in C2 are protected by cryptographic
     signatures made by a node that issued this information: object identifiers
     (including container identifiers and fids), capabilities, locks, layout
     identifiers, other resources identifiers, etc. reqh verifies authenticity
     of such information by fetching corresponding node keys, re-computing the
     signature locally and checking it with one in the fop;

   - resource limits: reqh estimates local resources (memory, cpu cycles,
     storage and network bandwidths) necessary for operation execution. The
     execution of operation is delayed if it would overload the server or
     exhaust resource quotas associated with operation source (client, group of
     clients, user, group of users, job, etc.);

   - resource usage and conflict resolution: reqh determines what distributed
     resources will be consumed by the operation execution and call resource
     management infrastructure to request the resources and deal with resource
     usage conflicts (by calling DLM if necessary);

   - object existence: reqh extracts identities of file system objects affected
     by the fop and requests appropriate stores to load object representations
     together with their basic attributes;

   - authorization control: reqh extracts the identity of a user (or users) on
     whose behalf the operation is executed. reqh then uses enterprise user data
     base to map user identities into internal form. Resulting internal user
     identifiers are matched against protection and authorization information
     stored in the file system objects (loaded on the previous step);

   - distributed transactions: for operations mutating file system state, reqh
     sets up local transaction context where the rest of the operation is
     executed.

   Once the standard actions are performed successfully, request handler
   delegates the rest of operation execution to the fom type specific state
   transition function.

   Fom execution proceeds as follows:

   @verbatim

	fop
	 |
	 v                fom->fo_state = FOS_READY
     c2_reqh_fop_handle()-------------->FOM
					 | fom->fo_state = FOS_RUNNING
					 v
				     FOPH_INIT
					 |
			failed		 v         fom->fo_state = FOS_WAITING
		     +<-----------FOPH_AUTHETICATE------------->+
		     |			 |           FOPH_AUTHENTICATE_WAIT
		     |			 v<---------------------+
		     +<----------FOPH_RESOURCE_LOCAL----------->+
		     |			 |           FOPH_RESOURCE_LOCAL_WAIT
		     |			 v<---------------------+
		     +<-------FOPH_RESOURCE_DISTRIBUTED-------->+
		     |			 |	  FOPH_RESOURCE_DISTRIBUTED_WAIT
		     |			 v<---------------------+
		     +<---------FOPH_OBJECT_CHECK-------------->+
		     |                   |              FOPH_OBJECT_CHECK
		     |		         v<---------------------+
		     +<---------FOPH_AUTHORISATION------------->+
		     |			 |            FOPH_AUTHORISATION
	             |	                 v<---------------------+
		     +<---------FOPH_TXN_CONTEXT--------------->+
		     |			 |            FOPH_TXN_CONTEXT_WAIT
		     |			 v<---------------------+
		     +<-------------FOPH_NR_+_1---------------->+
		     |			 |            FOPH_NR_+_1_WAIT
		     v			 v<---------------------+
		 FOPH_FAILED        FOPH_SUCCESS
		     |			 |
		     v			 v
	  +-----FOPH_TXN_ABORT    FOPH_TXN_COMMIT-------------->+
	  |	     |			 |            FOPH_TXN_COMMIT_WAIT
	  |	     |	    send reply	 v<---------------------+
	  |	     +----------->FOPH_QUEUE_REPLY------------->+
          |	     ^			 |            FOPH_QUEUE_REPLY_WAIT
	  v	     |			 v<---------------------+
   FOPH_TXN_ABORT_WAIT		     FOPH_FINISH ---> c2_fom_fini()

   @endverbatim

   If a generic phase handler function fails while executing a fom, then
   it just sets the c2_fom::fo_rc to the result of the operation and returns
   C2_FSO_WAIT.  c2_fom_tick_generic() then sets the c2_fom::fo_phase to
   C2_FOPH_FAILED, logs an ADDB event, and returns, later the fom execution
   proceeds as mentioned in above diagram.

   If fom fails while executing fop specific operation, the c2_fom::fo_phase
   is set to C2_FOPH_FAILED already by the fop specific operation handler, and
   the c2_fom::fo_rc set to the result of the operation.

   @see c2_fom_phase
   @see c2_fom_phase_outcome

   @param fom, fom under execution

   @retval C2_FSO_AGAIN, if fom operation is successful, transition to next
	   phase, C2_FSO_WAIT, if fom execution blocks and fom goes into
	   corresponding wait phase, or if fom execution is complete, i.e
	   success or failure

   @todo standard fom phases implementation, depends on the support routines for
	handling various standard operations on fop as mentioned above
 */
int c2_fom_tick_generic(struct c2_fom *fom);

/** @} end of fom group */

/* __COLIBRI_FOP_FOM_GENERIC_H__ */
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
