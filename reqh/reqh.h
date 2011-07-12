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
 * Original author: Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/04/2011
 */

#ifndef __COLIBRI_REQH_REQH_H__
#define __COLIBRI_REQH_REQH_H__

#include <sm/sm.h>
#include "fol/fol.h"

/* import */
struct c2_fop;
struct c2_fom;
struct c2_stob_domain;

/**
   @defgroup reqh Request handler

   @{
 */

struct c2_reqh;
struct c2_fop_sortkey;

/**
   Request handler instance.
 */
struct c2_reqh {
	struct c2_rpcmachine	*rh_rpc;
	struct c2_dtm		*rh_dtm;
	/**
	   @todo for now simply use storage object domain. In the future, this
	   will be replaced with "stores".
	 */
	struct c2_stob_domain	*rh_stdom;
	/** service this request hander belongs to */
	struct c2_service	*rh_serv;
	/** fol pointer for this request handler */
	struct c2_fol		*rh_fol;
	/** fom domain for this request handler */
	struct c2_fom_domain	 rh_fom_dom;
};

/**
   Initialises request handler instance provided by the caller.

   @see c2_reqh

   @pre reqh != NULL && stdom != NULL && fol != NULL && serv != NULL

   @retval 0, if request handler is succesfully initilaised,
		-errno, in case of failure
 */
int  c2_reqh_init(struct c2_reqh *reqh,
		struct c2_rpcmachine *rpc, struct c2_dtm *dtm,
		struct c2_stob_domain *stdom,
		struct c2_fol *fol, struct c2_service *serv);

/**
   Destructor for request handler, no fop will be further executed
   in the address space belonging to this request handler.

   @pre reqh != NULL
 */
void c2_reqh_fini(struct c2_reqh *reqh);

/**
   Sort-key determining global fop processing order.

   A sort-key is assigned to a fop when it enters NRS (Network Request
   Scheduler) incoming queue. NRS processes fops in sort-key order.

   @todo sort key definition.
 */
struct c2_fop_sortkey {
};

/**
   Submit fop for request handler processing.

   fop processing results are reported by other means (ADDB, reply fops, error
   messages, etc.) so this function returns nothing.

   @param reqh, request handler processing the fop
   @param fop, fop to be executed
   @param cookie, reply fop reference provided by the client

   @pre reqh != null.
   @pre fom != null.
 */
void c2_reqh_fop_handle(struct c2_reqh *reqh, struct c2_fop *fop, void *cookie);

/**
   Assign a sort-key to a fop.

   This function is called by NRS to order fops in its incoming queue.

   @todo -> to decide upon sort key generation parameters as required by
   nrs scheduler.
 */
void c2_reqh_fop_sortkey_get(struct c2_reqh *reqh, struct c2_fop *fop,
			     struct c2_fop_sortkey *key);


/**
   Standard fom state transition function.

   This function handles standard fom phases from enum c2_fom_phase.

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

   FOPH_INIT -> FOPH_AUTHENTICATE -> FOPH_RESOURCE_LOCAL ->
   FOPH_RESOURCE_DISTRIBUTED -> FOPH_OBJECT_CHECK -> FOPH_AUTHORISATION ->
   FOPH_TXN_CONTEXT -> FOPH_NR + 1 -> FOPH_QUEUE_REPLY

   Fom creates local transactional context and transitions into one of
   the non standard phases, having enumeration greater than FOPH_NR.
   On performing the non standard actions, fom transitions back to one of 
   the standard phases.

   Fom then sends the reply fop (or queueing it into fop cache in case of WBC), 
   and transitions back to one of the non standard phases. Later depending upon
   the status of fom execution, i.e success or failure, fom transitions back
   accordingly, see below

   FOPH_QUEUE_REPLY -> FOPH_NR + 1 -> FOPH_SUCCESS -> FOPH_TXN_COMMIT -> FOPH_DONE
   FOPH_QUEUE_REPLY-> FOPH_NR + 1 -> FOPH_FAILED -> FOPH_TXN_ABORT -> FOPH_DONE
   FOPH_DONE, fom execution is complete, i.e success or failure.

   If fom execution would block, in any of the transition phases, fom transitions 
   into the corresponding wait phase. Later, once the blocking operation is
   complete, fom execution is resumed by its wait phase handler and it transitions
   fom to next phase.

   @see c2_fom_phase
   @see c2_fom_state_outcome

   @retval FSO_AGAIN, if fom operation is successful, transition to next phase,
	FSO_WAIT, if fom execution is complete i.e success or failure

   @todo standard fom phases implementation, depends on the support routines for
   handling various standard operations on fop as mentioned above.
 */

int c2_fom_state_generic(struct c2_fom *fom);

/** @} endgroup reqh */

/* __COLIBRI_REQH_REQH_H__ */
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
